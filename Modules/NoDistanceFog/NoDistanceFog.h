#pragma once

#include "../../IModule.h"

// Forces distance fog off by intercepting the D3D9 device's SetRenderState and Draw* calls -
// ported from a tool by Celyanito, adapted to hook through Twinkie's own kiero instance instead
// of a separate manual VirtualProtect vtable patch and its own device lookup (Twinkie already has
// the device the moment kiero's own Reset/Present hooks go in - see NoDistanceFog_InstallHooks,
// called once from dllmain.cpp right after those).
//
// Fog has to be forced off before every draw call, not just once a frame: the game sets its own
// fog render states throughout its own scene rendering, which happens before Twinkie's Present
// hook ever runs - by the time hkPresent fires, the frame's geometry is already drawn. That's why
// this hooks SetRenderState (to override the game's own fog-enabling calls) and all four
// Draw*/DrawIndexed* variants (to reapply the fog-off state right before each one, in case
// something set it without going through the hooked SetRenderState).
//
// Known limitation: this only overrides the fixed-function D3DRS_FOG* render states. On higher
// in-game shader/graphics quality settings, TMF may render fog through a shader's own constants
// instead, which this can't touch - fog may still be visible there. Try a lower shader quality if
// it doesn't seem to be doing anything.
class NoDistanceFogModule : public IModule
{
public:
    NoDistanceFogModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "NoDistanceFog";
        this->FancyName       = "No Distance Fog";
        this->Category        = "Visual Effects";
    }

    virtual void RenderMenuItem() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return false; }
};

// Installs the vtable hooks via kiero - called once from dllmain.cpp's MainThread right after
// kiero::init(D3D9) and the Reset/Present binds succeed. The hooks themselves check a flag kept
// in sync by NoDistanceFog_SetEnabled (not RenderAnyways - TwinkUi only calls a module's
// RenderAnyways() while it's Enabled, so that would never fire on the frame you *disable* it,
// leaving fog permanently forced off once turned on) before doing anything, so installing them
// early and toggling that flag is enough - no separate install/uninstall dance needed.
void NoDistanceFog_InstallHooks();
void NoDistanceFog_SetEnabled(bool enabled);
