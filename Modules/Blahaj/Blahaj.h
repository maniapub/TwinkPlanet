#pragma once
#pragma execution_character_set("utf-8")

#include "../../IModule.h"
#include <d3d9.h>
#include <wincodec.h>
#include <vector>
#include <chrono>

class BlahajModule : public IModule
{
public:
    BlahajModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie   = &Twinkie;
        this->Logger    = &Logger;
        this->Name      = "Blahaj";
        this->FancyName = "Blahaj";
        this->Category  = "Visual Effects";
    }
    ~BlahajModule();

    virtual void RenderAnyways()  override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings() override { return true; }

private:
    struct GifFrame {
        IDirect3DTexture9* tex     = nullptr;
        unsigned int       msDelay = 100;
    };

    struct BlahajInst {
        float posX  = 100.f, posY  = 100.f;
        float sizeW = 200.f, sizeH = -1.f;  // -1 = use aspect-ratio default
        int   resizeAxis = 0;
        bool  sizeInit   = false;
    };

    std::vector<GifFrame>           m_Frames;
    std::vector<IDirect3DTexture9*> m_PendingRelease;
    int    m_CurFrame    = 0;
    std::chrono::steady_clock::time_point m_LastFrameTime;

    int   m_ImgW = 0, m_ImgH = 0;
    float m_AspectRatio    = 1.f;
    bool  m_Loaded          = false;
    bool  m_NeedsLoad       = true;
    std::string m_ErrorMsg;

    int                     m_Count = 1;
    std::vector<BlahajInst> m_Instances;

    void Load(IDirect3DDevice9* dev);
    bool DecodeWIC(IDirect3DDevice9* dev,
                   IWICImagingFactory* factory,
                   IWICBitmapDecoder* decoder);
};
