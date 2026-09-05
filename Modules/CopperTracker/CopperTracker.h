#pragma once

#include "../../IModule.h"
#include <vector>

class CopperTrackerModule : public IModule
{
public:
    CopperTrackerModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie  = &Twinkie;
        this->Logger   = &Logger;
        this->Name      = "CopperTracker";
        this->FancyName = "Copper Tracker";
        this->Category  = "Overlays";
    }

    virtual void Render()         override;
    virtual void RenderAnyways()  override;
    virtual void RenderInactive() override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings) override;
    // Settings panel made inaccessible (hidden from the Settings window's module list) - code
    // is left in place below, just unreachable from the UI.
    virtual bool HasSettings() override { return false; }

private:
    struct CopperEvent { int delta; };

    int  m_SessionStart  = -1;    // copper at session start; -1 = not yet sampled
    int  m_LastCopper    = -1;
    int  m_SessionDelta  = 0;
    bool m_ShowEventLog  = true;
    bool m_ShowSessionDelta = true;
    float m_Scale        = 1.0f;

    // Raw-scan helpers: -1 means "use GetCopper()", >= 0 means "read profile+offset"
    int  m_CopperOffset  = -1;
    int  m_DebugOffset   = 0;   // spinbox value in the scan UI

    std::vector<CopperEvent> m_Events;  // most recent changes, capped at kMaxEvents
    static constexpr int kMaxEvents = 50;
    static constexpr int kOverlayVisible = 10; // lines shown in the overlay

    // Position persistence
    ImVec2 m_WndPos    = { 0.f, 0.f };
    bool   m_WndPosSet = false;
};
