#pragma once

#include "../../IModule.h"
#include <d3d9.h>
#include <wincodec.h>
#include <vector>
#include <string>
#include <chrono>

class ImageModule : public IModule
{
public:
    ImageModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie  = &Twinkie;
        this->Logger   = &Logger;
        this->Name      = "Image";
        this->FancyName = "Image";
        this->Category  = "Visual Effects";
    }
    ~ImageModule();

    virtual void Render()         override;
    virtual void RenderAnyways()  override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings) override;
    virtual bool HasSettings() override { return true; }

    // -- Per-image data --
    struct GifFrame
    {
        IDirect3DTexture9* tex     = nullptr;
        unsigned int       msDelay = 100;
    };

    struct ImageEntry
    {
        static constexpr int kPathBufSize = 512;
        char pathBuf[kPathBufSize] = {};

        bool        loaded    = false;
        bool        needsLoad = false;
        int         imgW      = 0;
        int         imgH      = 0;
        std::string errorMsg;

        std::vector<GifFrame>           frames;
        int                             curFrame = 0;
        std::chrono::steady_clock::time_point lastFrameTime;

        std::vector<IDirect3DTexture9*> pendingRelease;

        float scale = 1.0f;
    };

private:
    std::vector<ImageEntry> m_Images;

    void TryLoad(ImageEntry& e);
    bool LoadWIC(ImageEntry& e, IDirect3DDevice9* dev, const std::wstring& path);
    bool LoadWICFromMemory(ImageEntry& e, IDirect3DDevice9* dev, const std::vector<BYTE>& data);
    bool DecodeDecoder(ImageEntry& e, IDirect3DDevice9* dev,
                       IWICImagingFactory* factory, IWICBitmapDecoder* decoder);
    void UnloadEntry(ImageEntry& e);
    IDirect3DTexture9* CurrentTex(const ImageEntry& e) const;
};
