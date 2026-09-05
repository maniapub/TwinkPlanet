#pragma once
#pragma execution_character_set("utf-8")

#include "../../IModule.h"
#include <string>

class GamingModeModule : public IModule
{
public:
    GamingModeModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "GamingMode";
        this->FancyName       = "GAMING Mode";
        this->Category        = "Visual Effects";
    }

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings() override { return true; }

private:
    float m_FontSize    = 80.f;
    float m_CycleSpeed  = 0.5f;
    float m_PosX        = 100.f;
    float m_PosY        = 100.f;

    // Empty defaults to "GAMING" - see DisplayText(). m_CustomTextBuf is the ImGui InputText
    // backing buffer (ImGui's InputText needs a raw char[], not std::string); m_CustomText is
    // the actual persisted value, kept in sync with the buffer each frame.
    std::string m_CustomText;
    char        m_CustomTextBuf[128] = "";
    std::string DisplayText() const { return m_CustomText.empty() ? "GAMING" : m_CustomText; }
};
