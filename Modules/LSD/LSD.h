#pragma once

#include "../../IModule.h"

class LSDModule : public IModule
{
public:
    LSDModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie  = &Twinkie;
        this->Logger   = &Logger;
        this->Name      = "Acid";
        this->FancyName = "Acid";
        this->Category  = "Visual Effects";
    }

    virtual void Render()         override;
    virtual void RenderAnyways()  override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings) override;
    virtual bool HasSettings() override { return true; }

private:
    float m_Hue              = 0.0f;   // current hue position [0,1)
    float m_Speed            = 0.4f;   // hue cycles per second
    float m_Alpha            = 0.25f;  // overlay opacity [0,1]
    bool  m_OnlyWhilePlaying = true;
};
