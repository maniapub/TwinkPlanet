#pragma once

#include "../../IModule.h"

class WheelIndicatorsModule : public IModule
{
public:
    WheelIndicatorsModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "WheelIndicators";
        this->FancyName       = "Wheel Indicators";
        this->Category        = "Overlays";
    }

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }

private:
    float m_WheelSize  = 18.f;   // circle radius in px
    float m_Opacity    = 0.90f;
    bool  m_ShowLabels = true;

    // Smoothed slip/contact values for fade transitions
    float m_SlipSmooth[4]    = {};
    float m_ContactSmooth[4] = {};

    // Position persistence
    ImVec2 m_WndPos    = { 0.f, 0.f };
    bool   m_WndPosSet = false;
};
