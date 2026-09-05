#define NOMINMAX
#pragma execution_character_set("utf-8")
#include "Colors.h"
#include <objbase.h>
#include <algorithm>
#include <vector>
#include "../../TwinkToast/TwinkToast.h"
#include "../../Utils.h"
#include "../../Resource.h"
#pragma comment(lib, "windowscodecs.lib")

// Wraps a resource's raw bytes in a WIC-readable stream, so the decoder below can read
// tmcolors.png straight out of the DLL's own memory instead of a file on disk.
static bool CreateWicStreamFromResource(IWICImagingFactory* Factory, int ResourceId, IWICStream** OutStream)
{
    const unsigned char* Data = nullptr;
    size_t Size = 0;
    if (!LoadEmbeddedResource(ResourceId, &Data, &Size)) return false;

    IWICStream* Stream = nullptr;
    if (FAILED(Factory->CreateStream(&Stream))) return false;
    if (FAILED(Stream->InitializeFromMemory((BYTE*)Data, (DWORD)Size)))
    {
        Stream->Release();
        return false;
    }

    *OutStream = Stream;
    return true;
}

static IDirect3DTexture9* ColorsMakeTex(IDirect3DDevice9* dev,
                                         const BYTE* bgra, UINT w, UINT h)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8,
                                  D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    const BYTE* src = bgra;
    BYTE*       dst = (BYTE*)lr.pBits;
    for (UINT y = 0; y < h; y++) { memcpy(dst, src, w * 4); src += w * 4; dst += lr.Pitch; }
    tex->UnlockRect(0);
    return tex;
}

void ColorsModule::Load(IDirect3DDevice9* dev)
{
    m_NeedsLoad = false;
    m_ErrorMsg.clear();

    HRESULT hr  = CoInitialize(nullptr);
    bool comOwn = (hr == S_OK);

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                CLSCTX_INPROC_SERVER,
                                IID_IWICImagingFactory, (void**)&factory)))
    {
        m_ErrorMsg = "WIC unavailable";
        if (comOwn) CoUninitialize();
        return;
    }

    // tmcolors.png is baked into the DLL as an RCDATA resource (see Twinkie.rc) rather than
    // loaded from Documents\TwinkPlanet\media\ - a fresh install no longer needs that file placed
    // there manually.
    IWICStream* stream = nullptr;
    if (!CreateWicStreamFromResource(factory, IDR_MEDIA_TMCOLORS, &stream))
    {
        m_ErrorMsg = "tmcolors.png resource missing";
        Logger->PrintError("[TM Colors] Embedded tmcolors.png resource could not be loaded.");
        TwinkToast::Get().Push("[TM Colors] Embedded tmcolors.png resource could not be loaded.",
                               8.f, { 1.f, 0.6f, 0.2f, 1.f });
        factory->Release();
        if (comOwn) CoUninitialize();
        return;
    }

    IWICBitmapDecoder* decoder = nullptr;
    HRESULT decodeHr = factory->CreateDecoderFromStream(stream, nullptr, WICDecodeMetadataCacheOnLoad, &decoder);
    stream->Release();
    if (FAILED(decodeHr))
    {
        m_ErrorMsg = "Failed to decode tmcolors.png";
        Logger->PrintError("[TM Colors] Failed to decode embedded tmcolors.png resource.");
        factory->Release();
        if (comOwn) CoUninitialize();
        return;
    }

    // ── Decode first frame ────────────────────────────────────────────────────
    IWICBitmapFrameDecode* frame = nullptr;
    UINT fw = 0, fh = 0;
    if (SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&fw, &fh)))
    {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(factory->CreateFormatConverter(&conv)))
        {
            conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA,
                             WICBitmapDitherTypeNone, nullptr, 0.0,
                             WICBitmapPaletteTypeCustom);
            std::vector<BYTE> px(fw * fh * 4);
            conv->CopyPixels(nullptr, fw * 4, (UINT)px.size(), px.data());
            conv->Release();

            m_Tex    = ColorsMakeTex(dev, px.data(), fw, fh);
            m_ImgW   = (int)fw;
            m_ImgH   = (int)fh;
            m_Aspect = (float)fw / (float)fh;
            m_Loaded = (m_Tex != nullptr);
        }
        frame->Release();
    }

    decoder->Release();
    factory->Release();
    if (comOwn) CoUninitialize();

    if (!m_Loaded)
        m_ErrorMsg = "Failed to decode image";
}

// Aspect-ratio size constraint (same pattern as Blahaj)
struct ColorsARData { float ratio; int* axis; };
static void ColorsARConstraint(ImGuiSizeCallbackData* d)
{
    auto* data = (ColorsARData*)d->UserData;
    int&  axis = *data->axis;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) axis = 0;
    if (axis == 0) {
        float dx = fabsf(d->DesiredSize.x - d->CurrentSize.x);
        float dy = fabsf(d->DesiredSize.y - d->CurrentSize.y);
        if (dx > 1.f || dy > 1.f) axis = (dx >= dy) ? 1 : 2;
    }
    if (axis == 2) d->DesiredSize.x = d->DesiredSize.y * data->ratio;
    else           d->DesiredSize.y = d->DesiredSize.x / data->ratio;
}

void ColorsModule::RenderAnyways()
{
    if (!Enabled) return;

    if (m_NeedsLoad) {
        IDirect3DDevice9* dev = Twinkie->GetD3DDevice();
        if (dev) Load(dev);
    }

    if (!m_Loaded && !m_NeedsLoad) {
        ImGui::SetNextWindowSize(ImVec2(420.f, 50.f), ImGuiCond_Always);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.05f, 0.05f, 0.92f));
        constexpr ImGuiWindowFlags kErrFlags =
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoNav;
        if (ImGui::Begin("TM Colors##colors_err", nullptr, kErrFlags))
            ImGui::TextWrapped("Place tmcolors.png in Documents\\TwinkPlanet\\media\\");
        ImGui::End();
        ImGui::PopStyleColor();
        return;
    }

    if (!m_Tex) return;

    bool menuVisible = *UiRenderEnabled;

    ColorsARData arData = { m_Aspect, &m_ResizeAxis };
    if (menuVisible)
        ImGui::SetNextWindowSizeConstraints(ImVec2(80.f, 40.f), ImVec2(8000.f, 8000.f),
                                            ColorsARConstraint, &arData);

    if (!m_SizeInit && m_Aspect > 0.f) {
        float h = m_SizeW / m_Aspect;
        ImGui::SetNextWindowSize(ImVec2(m_SizeW, h), ImGuiCond_Always);
        ImGui::SetNextWindowPos(ImVec2(m_PosX, m_PosY), ImGuiCond_Always);
        m_SizeInit = true;
    }

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,    ImVec2(0.f, 0.f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
    ImGui::SetNextWindowBgAlpha(0.f);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse  |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (!menuVisible)
        flags |= ImGuiWindowFlags_NoResize;

    if (!ImGui::Begin("##ColorsOverlay", nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    ImVec2 wsize = ImGui::GetWindowSize();
    ImVec2 wpos  = ImGui::GetWindowPos();
    m_SizeW = wsize.x; m_SizeH = wsize.y;
    m_PosX  = wpos.x;  m_PosY  = wpos.y;

    // Mouse-wheel zoom (always active)
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
    {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.f && m_Aspect > 0.f) {
            float newW = m_SizeW * (1.f + wheel * 0.08f);
            newW = std::max(120.f, std::min(newW, 6000.f));
            float newH = newW / m_Aspect;
            ImGui::SetWindowSize(ImVec2(newW, newH));
            m_SizeW = newW; m_SizeH = newH;
        }
    }

    // Drag to move — always enabled (grip only shown when menu open for resize handle)
    {
        const float grip = menuVisible ? 14.f : 0.f;
        ImVec2 btnSz = ImVec2(wsize.x - grip, wsize.y - grip);
        if (btnSz.x > 0.f && btnSz.y > 0.f) {
            ImGui::SetCursorPos(ImVec2(0.f, 0.f));
            ImGui::InvisibleButton("##drag", btnSz);
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 0.f)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                ImGui::SetWindowPos(ImVec2(wpos.x + delta.x, wpos.y + delta.y));
            }
        }
    }

    ImGui::SetCursorPos(ImVec2(0.f, 0.f));
    {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + wsize.x, p0.y + wsize.y);
        ImU32  tint = IM_COL32(255, 255, 255, (int)(m_Opacity * 255.f));
        ImGui::GetWindowDrawList()->AddImage((ImTextureID)m_Tex, p0, p1,
                                             ImVec2(0,0), ImVec2(1,1), tint);
        ImGui::Dummy(wsize);
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
}

void ColorsModule::RenderMenuItem()
{
    if (ImGui::MenuItem(ICON_FK_TINT " TM Colors", "", Enabled))
        Enabled = !Enabled;
}

void ColorsModule::RenderSettings()
{
    using namespace ImGui;
    SliderFloat("Opacity", &m_Opacity, 0.f, 1.f, "%.2f");
    TextDisabled("Scroll wheel over image to zoom in/out.");
    if (Button("Reset size")) { m_SizeW = 700.f; m_SizeH = -1.f; m_SizeInit = false; }
    SameLine();
    if (Button(ICON_FK_REFRESH " Reload image"))
    {
        if (m_Tex) { m_Tex->Release(); m_Tex = nullptr; }
        m_Loaded    = false;
        m_NeedsLoad = true;
        m_SizeInit  = false;
    }
    if (IsItemHovered())
        SetTooltip("Reloads tmcolors.png from Documents\\TwinkPlanet\\media\\");
}

void ColorsModule::SettingsInit(SettingMgr& Settings)
{
    Settings["Colors"]["Enable"].GetAsBool  (&Enabled);
    Settings["Colors"]["Opacity"].GetAsFloat(&m_Opacity);
    Settings["Colors"]["PosX"].GetAsFloat(&m_PosX);
    Settings["Colors"]["PosY"].GetAsFloat(&m_PosY);
    Settings["Colors"]["SizeW"].GetAsFloat(&m_SizeW);
    Settings["Colors"]["SizeH"].GetAsFloat(&m_SizeH);
    if (m_SizeW < 80.f) m_SizeW = 700.f;
}

void ColorsModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Colors"]["Enable"].Set(Enabled);
    Settings["Colors"]["Opacity"].Set(m_Opacity);
    Settings["Colors"]["PosX"].Set(m_PosX);
    Settings["Colors"]["PosY"].Set(m_PosY);
    Settings["Colors"]["SizeW"].Set(m_SizeW);
    Settings["Colors"]["SizeH"].Set(m_SizeH);
}
