#pragma once

#include "../../IModule.h"
#include <thread>
#include <mutex>
#include <atomic>
#include <complex>

class AudioVisModule : public IModule
{
public:
    AudioVisModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "AudioVis";
        this->FancyName       = "Audio Visualizer";
        this->Category        = "Overlays";

        // Pre-compute Hanning window
        for (int i = 0; i < kFFTSize; i++)
            m_Window[i] = 0.5f * (1.f - cosf(2.f * 3.14159265358979f * i / (kFFTSize - 1)));

        StartCapture();
    }

    ~AudioVisModule() override
    {
        StopCapture();
    }

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }

private:
    static constexpr int kFFTSize  = 2048;
    static constexpr int kRingSize = kFFTSize * 4;
    static constexpr int kMaxBars  = 64;

    // -- Audio capture --
    std::thread       m_CaptureThread;
    std::atomic<bool> m_Running{ false };
    std::mutex        m_Mutex;
    float             m_RingBuf[kRingSize] = {};
    int               m_RingWrite  = 0;
    int               m_SampleRate = 48000;

    // -- DSP state --
    float m_Window[kFFTSize] = {};
    float m_Bars[kMaxBars]    = {};
    float m_Peaks[kMaxBars]   = {};
    float m_PeakVel[kMaxBars] = {};   // gravity-accelerated peak fall velocity
    int   m_PeakHold[kMaxBars] = {};

    // -- Settings --
    int   m_NumBars     = 32;
    float m_Sensitivity = 1.5f;
    float m_FallSpeed   = 0.06f;
    float m_BarGap      = 2.f;
    float m_Opacity     = 0.85f;
    bool  m_ShowPeak    = false;
    int   m_ColorScheme = 0;   // 0=Rainbow 1=Fire 2=Green 3=Solid 4=Ocean 5=Sunset 6=Plasma
                               // 7=Ice 8=Lava 9=Neon 10=Retrowave 11=Gold 12=Mint 13=Matrix
                               // 14=Monochrome 15=Candy 16=Cosmic 17=CustomGradient
    float m_SolidColor[3]   = { 0.f, 1.f, 0.5f };
    float m_GradColor1[3]   = { 0.f, 0.f, 1.f };   // bottom color for custom gradient
    float m_GradColor2[3]   = { 1.f, 0.f, 0.5f };  // top color for custom gradient

    // -- Helpers --
    std::atomic<bool> m_NeedsRestart{ false };

    void StartCapture();
    void StopCapture();
    void CaptureLoop();
    void ProcessAudio();
    static void FFT(std::complex<float>* x, int n);

    // Position persistence
    ImVec2 m_WndPos    = { 0.f, 0.f };
    bool   m_WndPosSet = false;
};
