#pragma once

#include "../../IModule.h"
#include <Windows.h>

class PerformanceOverlayModule : public IModule
{
public:
    PerformanceOverlayModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "PerformanceOverlay";
        this->FancyName       = "Performance Overlay";
        this->Category        = "Overlays";
        Enabled               = true;
    }

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }

private:
    bool  m_ShowFPS       = true;
    bool  m_ShowFrameTime = true;
    bool  m_ShowCPU       = true;
    bool  m_ShowRAM       = true;
    bool  m_NoDecimals    = false;

    // Whether the display-refresh throttle below is active at all. This is
    // the only thing exposed to the user - the amount (kDisplayRefreshInterval)
    // stays fixed either way, so there's no slider for "how slow".
    bool  m_SlowUpdate    = true;

    // How often the *displayed* numbers refresh when m_SlowUpdate is on. Not
    // user-adjustable - it's just here to stop fast-jittering stats (FPS
    // especially) from flickering a new number every single frame. A fixed,
    // small delay reads as "smoothed", not "laggy".
    static constexpr float kDisplayRefreshInterval = 0.2f; // 200 ms
    float m_DisplayRefreshTimer = 0.f;
    float m_DisplayFps    = 0.f;
    float m_DisplayMs     = 0.f;
    float m_DisplayCpu    = 0.f;
    DWORD m_DisplayRamUsedMB  = 0;
    DWORD m_DisplayRamTotalMB = 0;

    // CPU tracking (sampled every 0.5s via GetSystemTimes, independent of the
    // display refresh above - this is the actual measurement cadence, not
    // just how often the number on screen changes)
    float          m_CpuUsage       = 0.f;
    float          m_CpuTimer       = 0.f;
    bool           m_CpuInitialized = false;
    ULARGE_INTEGER m_LastIdle       = {};
    ULARGE_INTEGER m_LastKernel     = {};
    ULARGE_INTEGER m_LastUser       = {};

    void UpdateCPU(float dt);

    static ImVec4 FpsColor(float fps);
    static ImVec4 CpuColor(float pct);

    // Position persistence
    ImVec2 m_WndPos    = { 0.f, 0.f };
    bool   m_WndPosSet = false;
};
