#pragma once
#include "../../IModule.h"
#include <chrono>
#include <ctime>

class ClockSessionTimerModule : public IModule
{
    using SteadyClock = std::chrono::steady_clock;

    // -- Clock --
    bool   m_ShowClock     = true;
    ImVec2 m_ClockPos      = { 10.f, 10.f };
    bool   m_ClockPosSet   = false;
    bool   m_Show24h       = true;
    bool   m_ShowDate      = false;
    bool   m_ShowSecs      = true;
    float  m_ClockFontScale = 1.5f;
    ImVec4 m_ClockColor    = { 1.f, 1.f, 1.f, 1.f };
    float  m_ClockBgAlpha  = 0.6f;

    // -- Session timer --
    bool   m_ShowSession       = true;
    SteadyClock::time_point m_SessionStart = SteadyClock::now();
    ImVec2 m_SessionPos        = { 10.f, 60.f };
    bool   m_SessionPosSet     = false;
    float  m_SessionFontScale  = 1.5f;
    ImVec4 m_SessionColor      = { 1.f, 1.f, 1.f, 1.f };
    float  m_SessionBgAlpha    = 0.6f;

public:
    ClockSessionTimerModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie   = &Twinkie;
        this->Logger    = &Logger;
        this->Name      = "ClockSessionTimer";
        this->FancyName = "Clock & Session Timer";
        this->Category  = "Overlays";
    }

    virtual void RenderAnyways()  override;
    virtual void RenderSettings() override;
    virtual void RenderMenuItem() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings() override { return true; }

private:
    void RenderClockWindow();
    void RenderSessionWindow();
};
