#include "NoDistanceFog.h"
#include "../../kiero/kiero.h"
#include <cstring>

namespace
{
    // Only ever read/written on the game's render thread (RenderAnyways and the hooks below both
    // run there - the hooks fire as part of the game's own per-frame scene rendering, which
    // happens on the same thread that calls Present), so a plain bool is enough - this is checked
    // on every single draw call, so it stays as cheap as possible.
    bool g_FogOverrideEnabled = false;

    using SetRenderStateFn         = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DRENDERSTATETYPE, DWORD);
    using DrawPrimitiveFn          = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT);
    using DrawIndexedPrimitiveFn   = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, INT, UINT, UINT, UINT, UINT);
    using DrawPrimitiveUPFn        = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, const void*, UINT);
    using DrawIndexedPrimitiveUPFn = HRESULT(APIENTRY*)(IDirect3DDevice9*, D3DPRIMITIVETYPE, UINT, UINT, UINT, const void*, D3DFORMAT, const void*, UINT);

    constexpr uint16_t kSetRenderStateVTableIndex       = 57;
    constexpr uint16_t kDrawPrimitiveVTableIndex        = 81;
    constexpr uint16_t kDrawIndexedPrimitiveVTableIndex = 82;
    constexpr uint16_t kDrawPrimitiveUPVTableIndex      = 83;
    constexpr uint16_t kDrawIndexedPrimitiveUPVTableIndex = 84;

    SetRenderStateFn         g_originalSetRenderState         = nullptr;
    DrawPrimitiveFn          g_originalDrawPrimitive           = nullptr;
    DrawIndexedPrimitiveFn   g_originalDrawIndexedPrimitive    = nullptr;
    DrawPrimitiveUPFn        g_originalDrawPrimitiveUP         = nullptr;
    DrawIndexedPrimitiveUPFn g_originalDrawIndexedPrimitiveUP  = nullptr;

    DWORD FloatAsDword(float value)
    {
        DWORD result = 0;
        memcpy(&result, &value, sizeof(result));
        return result;
    }

    void ForceFogOff(IDirect3DDevice9* device)
    {
        if (!device || !g_originalSetRenderState) return;

        g_originalSetRenderState(device, D3DRS_FOGENABLE, FALSE);
        g_originalSetRenderState(device, D3DRS_RANGEFOGENABLE, FALSE);
        g_originalSetRenderState(device, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
        g_originalSetRenderState(device, D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
        g_originalSetRenderState(device, D3DRS_FOGDENSITY, FloatAsDword(0.0f));
        g_originalSetRenderState(device, D3DRS_FOGSTART, FloatAsDword(1000000.0f));
        g_originalSetRenderState(device, D3DRS_FOGEND, FloatAsDword(1000000.0f));
    }

    HRESULT APIENTRY HookSetRenderState(IDirect3DDevice9* device, D3DRENDERSTATETYPE state, DWORD value)
    {
        if (g_FogOverrideEnabled)
        {
            switch (state)
            {
            case D3DRS_FOGENABLE:
            case D3DRS_RANGEFOGENABLE:
                return g_originalSetRenderState(device, state, FALSE);
            case D3DRS_FOGTABLEMODE:
            case D3DRS_FOGVERTEXMODE:
                return g_originalSetRenderState(device, state, D3DFOG_NONE);
            case D3DRS_FOGDENSITY:
                return g_originalSetRenderState(device, state, FloatAsDword(0.0f));
            case D3DRS_FOGSTART:
            case D3DRS_FOGEND:
                return g_originalSetRenderState(device, state, FloatAsDword(1000000.0f));
            default:
                break;
            }
        }
        return g_originalSetRenderState(device, state, value);
    }

    HRESULT APIENTRY HookDrawPrimitive(IDirect3DDevice9* device, D3DPRIMITIVETYPE primitiveType, UINT startVertex, UINT primitiveCount)
    {
        if (g_FogOverrideEnabled) ForceFogOff(device);
        return g_originalDrawPrimitive(device, primitiveType, startVertex, primitiveCount);
    }

    HRESULT APIENTRY HookDrawIndexedPrimitive(
        IDirect3DDevice9* device,
        D3DPRIMITIVETYPE primitiveType,
        INT baseVertexIndex,
        UINT minVertexIndex,
        UINT numVertices,
        UINT startIndex,
        UINT primitiveCount)
    {
        if (g_FogOverrideEnabled) ForceFogOff(device);
        return g_originalDrawIndexedPrimitive(device, primitiveType, baseVertexIndex, minVertexIndex, numVertices, startIndex, primitiveCount);
    }

    HRESULT APIENTRY HookDrawPrimitiveUP(
        IDirect3DDevice9* device,
        D3DPRIMITIVETYPE primitiveType,
        UINT primitiveCount,
        const void* vertexStreamZeroData,
        UINT vertexStreamZeroStride)
    {
        if (g_FogOverrideEnabled) ForceFogOff(device);
        return g_originalDrawPrimitiveUP(device, primitiveType, primitiveCount, vertexStreamZeroData, vertexStreamZeroStride);
    }

    HRESULT APIENTRY HookDrawIndexedPrimitiveUP(
        IDirect3DDevice9* device,
        D3DPRIMITIVETYPE primitiveType,
        UINT minVertexIndex,
        UINT numVertices,
        UINT primitiveCount,
        const void* indexData,
        D3DFORMAT indexDataFormat,
        const void* vertexStreamZeroData,
        UINT vertexStreamZeroStride)
    {
        if (g_FogOverrideEnabled) ForceFogOff(device);
        return g_originalDrawIndexedPrimitiveUP(device, primitiveType, minVertexIndex, numVertices, primitiveCount, indexData, indexDataFormat, vertexStreamZeroData, vertexStreamZeroStride);
    }
}

void NoDistanceFog_InstallHooks()
{
    if (g_originalSetRenderState) return; // already installed

    kiero::bind(kSetRenderStateVTableIndex,       (void**)&g_originalSetRenderState,        (void*)&HookSetRenderState);
    kiero::bind(kDrawPrimitiveVTableIndex,        (void**)&g_originalDrawPrimitive,         (void*)&HookDrawPrimitive);
    kiero::bind(kDrawIndexedPrimitiveVTableIndex, (void**)&g_originalDrawIndexedPrimitive,  (void*)&HookDrawIndexedPrimitive);
    kiero::bind(kDrawPrimitiveUPVTableIndex,      (void**)&g_originalDrawPrimitiveUP,       (void*)&HookDrawPrimitiveUP);
    kiero::bind(kDrawIndexedPrimitiveUPVTableIndex, (void**)&g_originalDrawIndexedPrimitiveUP, (void*)&HookDrawIndexedPrimitiveUP);
}

void NoDistanceFog_SetEnabled(bool enabled)
{
    g_FogOverrideEnabled = enabled;
}

void NoDistanceFogModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_CLOUD " No Distance Fog", "", Enabled))
    {
        Enabled = !Enabled;
        NoDistanceFog_SetEnabled(Enabled);
    }
    if (IsItemHovered())
        SetTooltip("Forces distance fog off. If fog still shows up, try lowering the game's\nshader/graphics quality - on higher settings TMF can render fog through a\nshader instead of the fixed-function states this overrides.");
}

void NoDistanceFogModule::SettingsInit(SettingMgr& Settings)
{
    Settings["NoDistanceFog"]["Enable"].GetAsBool(&Enabled);
    NoDistanceFog_SetEnabled(Enabled); // in case it was left on from a previous game launch
}

void NoDistanceFogModule::SettingsSave(SettingMgr& Settings)
{
    Settings["NoDistanceFog"]["Enable"].Set(Enabled);
}
