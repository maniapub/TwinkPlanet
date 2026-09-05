#pragma once
#include "../../IModule.h"
#include <d3d9.h>
#include <wincodec.h>

class ColorsModule : public IModule
{
    IDirect3DTexture9* m_Tex       = nullptr;
    int   m_ImgW      = 0, m_ImgH = 0;
    float m_Aspect    = 1.f;
    bool  m_Loaded    = false;
    bool  m_NeedsLoad = true;
    std::string m_ErrorMsg;

    float m_SizeW = 700.f, m_SizeH = -1.f;
    float m_PosX  = 100.f, m_PosY  = 100.f;
    bool  m_SizeInit   = false;
    int   m_ResizeAxis = 0;
    float m_Opacity    = 0.95f;

    void Load(IDirect3DDevice9* dev);

public:
    ColorsModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie   = &Twinkie;
        this->Logger    = &Logger;
        this->Name      = "Colors";
        this->FancyName = "TM Colors";
        this->Category  = "Visual Effects";
    }
    ~ColorsModule() { if (m_Tex) { m_Tex->Release(); m_Tex = nullptr; } }

    virtual void RenderAnyways()  override;
    virtual void RenderSettings() override;
    virtual void RenderMenuItem() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings() override { return true; }
};
