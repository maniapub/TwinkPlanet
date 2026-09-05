#pragma once
#include "../../IModule.h"
#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <mmdeviceapi.h>
#include <endpointvolume.h>

class MediaControlsModule : public IModule
{
public:
    MediaControlsModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie   = &Twinkie;
        this->Logger    = &Logger;
        this->Name      = "MediaControls";
        this->FancyName = "Media Controls";
        this->Category  = "Integrations";
        InitAudio();
        StartSMTCThread();
    }

    ~MediaControlsModule() override
    {
        m_SMTCRunning = false;
        if (m_SMTCThread.joinable())
            m_SMTCThread.join();
        if (m_VolumeEndpoint) { m_VolumeEndpoint->Release(); m_VolumeEndpoint = nullptr; }
    }

    virtual void RenderAnyways()  override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings) override;
    virtual bool HasSettings() override { return true; }

private:
    // SMTC polling thread
    std::thread       m_SMTCThread;
    std::atomic<bool> m_SMTCRunning{ false };
    std::mutex        m_Mutex;
    std::string       m_Title;
    std::string       m_Artist;
    bool              m_IsPlaying = false;
    bool              m_SMTCAvail = false;   // false if WinRT unavailable

    // WASAPI volume
    IAudioEndpointVolume* m_VolumeEndpoint = nullptr;
    float m_Volume  = 1.0f;
    bool  m_IsMuted = false;

    // Position
    ImVec2 m_WndPos    = { 20.f, 20.f };
    bool   m_WndPosSet = false;

    void InitAudio();
    void RefreshAudio();
    void StartSMTCThread();
};
