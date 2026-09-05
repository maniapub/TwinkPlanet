#define NOMINMAX
#include "AudioVis.h"

#include <mmdeviceapi.h>
#include <audioclient.h>
#include <cmath>
#include <algorithm>

#pragma comment(lib, "ole32.lib")

// =============================================================================
// FFT  (in-place radix-2 Cooley-Tukey DIT)
// =============================================================================

void AudioVisModule::FFT(std::complex<float>* x, int n)
{
    // Bit-reversal permutation
    for (int i = 1, j = 0; i < n; i++) {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(x[i], x[j]);
    }
    // Butterfly passes
    for (int len = 2; len <= n; len <<= 1) {
        float ang = -2.f * 3.14159265358979f / (float)len;
        std::complex<float> wlen(cosf(ang), sinf(ang));
        for (int i = 0; i < n; i += len) {
            std::complex<float> w(1.f, 0.f);
            for (int j = 0; j < len / 2; j++) {
                std::complex<float> u = x[i + j];
                std::complex<float> v = x[i + j + len / 2] * w;
                x[i + j]             = u + v;
                x[i + j + len / 2]   = u - v;
                w *= wlen;
            }
        }
    }
}

// =============================================================================
// WASAPI loopback capture thread
// =============================================================================

void AudioVisModule::StartCapture()
{
    m_Running = true;
    m_CaptureThread = std::thread([this]() { CaptureLoop(); });
}

void AudioVisModule::StopCapture()
{
    m_Running = false;
    if (m_CaptureThread.joinable())
        m_CaptureThread.join();
}

void AudioVisModule::CaptureLoop()
{
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    IMMDeviceEnumerator*  pEnum    = nullptr;
    IMMDevice*            pDevice  = nullptr;
    IAudioClient*         pClient  = nullptr;
    IAudioCaptureClient*  pCapture = nullptr;
    WAVEFORMATEX*         pwfx     = nullptr;

    auto Cleanup = [&]() {
        if (pClient)  pClient->Stop();
        if (pwfx)     CoTaskMemFree(pwfx);
        if (pCapture) { pCapture->Release(); pCapture = nullptr; }
        if (pClient)  { pClient->Release();  pClient  = nullptr; }
        if (pDevice)  { pDevice->Release();  pDevice  = nullptr; }
        if (pEnum)    { pEnum->Release();    pEnum    = nullptr; }
        CoUninitialize();
    };

    HRESULT hr;
    hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL,
                          __uuidof(IMMDeviceEnumerator), (void**)&pEnum);
    if (FAILED(hr)) { Cleanup(); return; }

    hr = pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    if (FAILED(hr)) { Cleanup(); return; }

    hr = pDevice->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&pClient);
    if (FAILED(hr)) { Cleanup(); return; }

    hr = pClient->GetMixFormat(&pwfx);
    if (FAILED(hr)) { Cleanup(); return; }

    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        m_SampleRate = (int)pwfx->nSamplesPerSec;
    }

    const REFERENCE_TIME kBufDuration = 200000; // 20 ms
    hr = pClient->Initialize(AUDCLNT_SHAREMODE_SHARED, AUDCLNT_STREAMFLAGS_LOOPBACK,
                             kBufDuration, 0, pwfx, NULL);
    if (FAILED(hr)) { Cleanup(); return; }

    hr = pClient->GetService(__uuidof(IAudioCaptureClient), (void**)&pCapture);
    if (FAILED(hr)) { Cleanup(); return; }

    // Determine sample format
    bool isFloat = (pwfx->wFormatTag == WAVE_FORMAT_IEEE_FLOAT);
    if (pwfx->wFormatTag == WAVE_FORMAT_EXTENSIBLE) {
        auto* ext = reinterpret_cast<WAVEFORMATEXTENSIBLE*>(pwfx);
        // KSDATAFORMAT_SUBTYPE_IEEE_FLOAT
        static const GUID kSubFloat =
            {0x00000003, 0x0000, 0x0010, {0x80,0x00,0x00,0xaa,0x00,0x38,0x9b,0x71}};
        isFloat = (ext->SubFormat == kSubFloat);
    }
    int channels = (int)pwfx->nChannels;

    hr = pClient->Start();
    if (FAILED(hr)) { Cleanup(); return; }

    while (m_Running.load()) {
        UINT32 packetLen = 0;
        if (FAILED(pCapture->GetNextPacketSize(&packetLen))) break;

        while (packetLen != 0) {
            BYTE*  pData   = nullptr;
            UINT32 frames  = 0;
            DWORD  flags   = 0;

            if (FAILED(pCapture->GetBuffer(&pData, &frames, &flags, NULL, NULL))) break;

            {
                std::lock_guard<std::mutex> lock(m_Mutex);
                if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) || !isFloat) {
                    // Write silence so ring buffer advances
                    for (UINT32 f = 0; f < frames; f++) {
                        m_RingBuf[m_RingWrite & (kRingSize - 1)] = 0.f;
                        m_RingWrite++;
                    }
                } else {
                    float* fData = reinterpret_cast<float*>(pData);
                    for (UINT32 f = 0; f < frames; f++) {
                        float mono = 0.f;
                        for (int ch = 0; ch < channels; ch++)
                            mono += fData[f * channels + ch];
                        mono /= (float)channels;
                        m_RingBuf[m_RingWrite & (kRingSize - 1)] = mono;
                        m_RingWrite++;
                    }
                }
            }

            if (FAILED(pCapture->ReleaseBuffer(frames))) break;
            if (FAILED(pCapture->GetNextPacketSize(&packetLen))) break;
        }

        Sleep(5);
    }

    Cleanup();
}

// =============================================================================
// DSP: read ring buffer, FFT, map to bars
// =============================================================================

void AudioVisModule::ProcessAudio()
{
    float samples[kFFTSize];
    int   sampleRate;
    {
        std::lock_guard<std::mutex> lock(m_Mutex);
        sampleRate = m_SampleRate;
        int base = m_RingWrite - kFFTSize;
        for (int i = 0; i < kFFTSize; i++) {
            int idx = (base + i) & (kRingSize - 1);
            samples[i] = m_RingBuf[idx];
        }
    }

    // Build FFT input with Hanning window
    std::complex<float> fftBuf[kFFTSize];
    for (int i = 0; i < kFFTSize; i++)
        fftBuf[i] = { samples[i] * m_Window[i], 0.f };

    FFT(fftBuf, kFFTSize);

    // Map bins to bars (log frequency scale  40 Hz - 16 kHz)
    const float fMin = 40.f, fMax = 16000.f;
    const float logRatio = logf(fMax / fMin);

    for (int b = 0; b < m_NumBars; b++) {
        float fLow  = fMin * expf(logRatio * (float)b       / (float)m_NumBars);
        float fHigh = fMin * expf(logRatio * (float)(b + 1) / (float)m_NumBars);

        int binLow  = (int)(fLow  * kFFTSize / (float)sampleRate);
        int binHigh = (int)(fHigh * kFFTSize / (float)sampleRate);
        binLow  = std::max(1,            std::min(binLow,  kFFTSize / 2 - 1));
        binHigh = std::max(binLow + 1,   std::min(binHigh, kFFTSize / 2));

        float sum = 0.f;
        for (int k = binLow; k < binHigh; k++)
            sum += std::abs(fftBuf[k]);
        sum /= (float)(binHigh - binLow);

        // dB normalization
        float db    = 20.f * log10f(sum / (float)kFFTSize + 1e-9f);
        float level = (db + 80.f) / 80.f * m_Sensitivity;
        level = std::max(0.f, std::min(level, 1.f));

        // Exponential envelope: fast attack, smooth release
        const float kRiseRate = 0.72f;
        float fallRate = m_FallSpeed * 0.35f;
        if (level > m_Bars[b])
            m_Bars[b] += (level - m_Bars[b]) * kRiseRate;
        else
            m_Bars[b] += (level - m_Bars[b]) * fallRate;
        m_Bars[b] = std::max(0.f, m_Bars[b]);

        // Peak hold with gravity-accelerated fall
        if (level >= m_Peaks[b]) {
            m_Peaks[b]    = level;
            m_PeakHold[b] = 45;
            m_PeakVel[b]  = 0.f;
        } else if (m_PeakHold[b] > 0) {
            m_PeakHold[b]--;
        } else {
            m_PeakVel[b] += 0.0015f;
            m_Peaks[b]    = std::max(0.f, m_Peaks[b] - m_PeakVel[b]);
        }
    }

    // 3-tap frequency smoothing for an organic, less jagged look
    float tmp[kMaxBars];
    for (int b = 0; b < m_NumBars; b++) {
        float lo = (b > 0)            ? m_Bars[b - 1] : m_Bars[b];
        float hi = (b < m_NumBars-1)  ? m_Bars[b + 1] : m_Bars[b];
        tmp[b] = m_Bars[b] * 0.5f + lo * 0.25f + hi * 0.25f;
    }
    for (int b = 0; b < m_NumBars; b++) m_Bars[b] = tmp[b];
}

// =============================================================================
// IModule overrides
// =============================================================================

void AudioVisModule::RenderAnyways()
{
    if (!Enabled) return;

    if (m_NeedsRestart.exchange(false)) {
        StopCapture();
        StartCapture();
    }

    ProcessAudio();

    using namespace ImGui;

    auto flags = ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoScrollbar
               | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoFocusOnAppearing
               | ImGuiWindowFlags_NoCollapse;

    if (!*UiRenderEnabled)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

    SetNextWindowBgAlpha(m_Opacity);
    SetNextWindowSize(ImVec2(500.f, 120.f), ImGuiCond_FirstUseEver);
    if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
    else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }

    if (!Begin("##AudioVis", nullptr, flags)) { End(); return; }
    m_WndPos = GetWindowPos();

    ImVec2 sz     = GetContentRegionAvail();
    ImDrawList* dl = GetWindowDrawList();
    ImVec2 origin = GetCursorScreenPos();

    float totalGap = m_BarGap * (float)(m_NumBars - 1);
    float barW     = (sz.x - totalGap) / (float)m_NumBars;
    if (barW < 1.f) barW = 1.f;

    float rounding = std::min(barW * 0.35f, 4.f);

    // Draws a vertical gradient bar (no corner rounding - incompatible with multi-colour fill)
    auto GradBar = [&](float x0, float y0, float x1, float y1, ImU32 colTop, ImU32 colBot) {
        dl->AddRectFilledMultiColor(ImVec2(x0, y0), ImVec2(x1, y1), colTop, colTop, colBot, colBot);
    };

    for (int i = 0; i < m_NumBars; i++) {
        float x0   = origin.x + (float)i * (barW + m_BarGap);
        float barH = m_Bars[i] * sz.y;
        float y0   = origin.y + sz.y - barH;
        float y1   = origin.y + sz.y;

        if (barH < 1.f) continue;

        float t = (float)i / (float)(m_NumBars - 1);  // bar position 0..1

        ImU32 peakCol = IM_COL32(255, 255, 255, 200);  // fallback peak colour

        switch (m_ColorScheme) {
        case 1: { // Fire
            ImU32 top = IM_COL32(255, (int)(200 + t * 55), (int)(t * 40), 255);
            ImU32 bot = IM_COL32(180, 20, 0, 255);
            GradBar(x0, y0, x0 + barW, y1, top, bot);
            peakCol = IM_COL32(255, (int)(t * 80), 0, 200);
            break;
        }
        case 2: { // Green
            float v = 0.55f + m_Bars[i] * 0.45f;
            ImU32 col = IM_COL32(0, (int)(v * 220), (int)(v * 60), 255);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1),
                              col, rounding, ImDrawFlags_RoundCornersTop);
            peakCol = IM_COL32(120, 255, 120, 200);
            break;
        }
        case 3: { // Solid
            ImU32 col = IM_COL32((int)(m_SolidColor[0]*255),
                                  (int)(m_SolidColor[1]*255),
                                  (int)(m_SolidColor[2]*255), 255);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1),
                              col, rounding, ImDrawFlags_RoundCornersTop);
            peakCol = IM_COL32((int)(m_SolidColor[0]*200),(int)(m_SolidColor[1]*200),(int)(m_SolidColor[2]*200),200);
            break;
        }
        case 4: { // Ocean
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(0, 210, 255, 255), IM_COL32(0, 30, 120, 255));
            peakCol = IM_COL32(100, 230, 255, 200);
            break;
        }
        case 5: { // Sunset
            int sr = (int)(180 + t * 75), sg = (int)(t * 120), sb = (int)(220 - t * 220);
            GradBar(x0, y0, x0 + barW, y1,
                    IM_COL32(sr, sg, sb, 255), IM_COL32(sr/3, sg/3, sb/3, 255));
            peakCol = IM_COL32(sr, sg, sb, 200);
            break;
        }
        case 6: { // Plasma
            int pr = (int)(80 + t * 175), pg = (int)(t * 220), pb = (int)(220 - t * 220);
            GradBar(x0, y0, x0 + barW, y1,
                    IM_COL32(pr, pg, pb, 255), IM_COL32(40, 0, 80, 255));
            peakCol = IM_COL32(pr, pg, pb, 200);
            break;
        }
        case 7: { // Ice
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(210, 245, 255, 255), IM_COL32(0, 30, 90, 255));
            peakCol = IM_COL32(210, 245, 255, 200);
            break;
        }
        case 8: { // Lava
            int tg = (int)(m_Bars[i] * 210);
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(255, tg, 0, 255), IM_COL32(20, 0, 0, 255));
            peakCol = IM_COL32(255, tg, 0, 200);
            break;
        }
        case 9: { // Neon
            static const float kNH[] = { 0.83f, 0.52f, 0.36f, 0.16f };
            float r, g, b;
            ColorConvertHSVtoRGB(kNH[i % 4], 1.f, 1.f, r, g, b);
            ImU32 bright = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255);
            ImU32 dark   = IM_COL32((int)(r*50), (int)(g*50), (int)(b*50), 255);
            GradBar(x0, y0, x0 + barW, y1, bright, dark);
            peakCol = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),200);
            break;
        }
        case 10: { // Retrowave
            int rr = (int)((1.f - t) * 255);
            ImU32 top = IM_COL32(rr, (int)(t * 200), 255, 255);
            ImU32 bot = IM_COL32(rr/4, 0, 60, 255);
            GradBar(x0, y0, x0 + barW, y1, top, bot);
            peakCol = IM_COL32(rr, (int)(t * 200), 255, 200);
            break;
        }
        case 11: { // Gold
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(255, 220, 30, 255), IM_COL32(80, 40, 0, 255));
            peakCol = IM_COL32(255, 235, 120, 200);
            break;
        }
        case 12: { // Mint
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(150, 255, 210, 255), IM_COL32(0, 60, 50, 255));
            peakCol = IM_COL32(150, 255, 210, 200);
            break;
        }
        case 13: { // Matrix
            GradBar(x0, y0, x0 + barW, y1, IM_COL32(0, 255, 60, 255), IM_COL32(0, 20, 0, 255));
            peakCol = IM_COL32(0, 255, 60, 200);
            break;
        }
        case 14: { // Monochrome
            float v = 0.35f + m_Bars[i] * 0.65f;
            ImU32 col = IM_COL32((int)(v*255),(int)(v*255),(int)(v*255),255);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1),
                              col, rounding, ImDrawFlags_RoundCornersTop);
            peakCol = IM_COL32(255, 255, 255, 200);
            break;
        }
        case 15: { // Candy (pastel rainbow)
            float r, g, b;
            ColorConvertHSVtoRGB((float)i / (float)m_NumBars, 0.42f, 1.f, r, g, b);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1),
                              IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255),
                              rounding, ImDrawFlags_RoundCornersTop);
            peakCol = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),200);
            break;
        }
        case 16: { // Cosmic (rainbow with vertical gradient)
            float r, g, b;
            ColorConvertHSVtoRGB((float)i / (float)m_NumBars, 0.9f, 1.f, r, g, b);
            GradBar(x0, y0, x0 + barW, y1,
                    IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255),
                    IM_COL32((int)(r*40), (int)(g*40), (int)(b*40), 255));
            peakCol = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),200);
            break;
        }
        case 17: { // Custom Gradient
            ImU32 top = IM_COL32((int)(m_GradColor2[0]*255),(int)(m_GradColor2[1]*255),(int)(m_GradColor2[2]*255),255);
            ImU32 bot = IM_COL32((int)(m_GradColor1[0]*255),(int)(m_GradColor1[1]*255),(int)(m_GradColor1[2]*255),255);
            GradBar(x0, y0, x0 + barW, y1, top, bot);
            peakCol = IM_COL32((int)(m_GradColor2[0]*200),(int)(m_GradColor2[1]*200),(int)(m_GradColor2[2]*200),200);
            break;
        }
        default: { // Rainbow
            float r, g, b;
            ColorConvertHSVtoRGB((float)i / (float)m_NumBars, 0.85f, 0.9f, r, g, b);
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x0 + barW, y1),
                              IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255),
                              rounding, ImDrawFlags_RoundCornersTop);
            peakCol = IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),200);
            break;
        }
        }

        // Peak marker - 3 px tall
        if (m_ShowPeak && m_Peaks[i] > 0.02f) {
            float py = origin.y + sz.y - m_Peaks[i] * sz.y;
            dl->AddRectFilled(ImVec2(x0, py - 3.f), ImVec2(x0 + barW, py),
                              peakCol, rounding, ImDrawFlags_RoundCornersTop);
        }
    }

    Dummy(sz);
    End();
}

void AudioVisModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_MUSIC " Audio Visualizer", "", Enabled))
        Enabled = !Enabled;
}

void AudioVisModule::RenderSettings()
{
    using namespace ImGui;
    static const char* kSchemes[] = {
        "Rainbow", "Fire", "Green", "Solid",
        "Ocean", "Sunset", "Plasma", "Ice",
        "Lava", "Neon", "Retrowave", "Gold",
        "Mint", "Matrix", "Monochrome", "Candy",
        "Cosmic", "Custom Gradient"
    };
    Combo("Color scheme", &m_ColorScheme, kSchemes, IM_ARRAYSIZE(kSchemes));

    if (m_ColorScheme == 3)
        ColorEdit3("Color", m_SolidColor);
    if (m_ColorScheme == 17) {
        ColorEdit3("Bottom color", m_GradColor1);
        ColorEdit3("Top color",    m_GradColor2);
    }

    SliderInt  ("Bars",        &m_NumBars,     8,   kMaxBars);
    SliderFloat("Sensitivity", &m_Sensitivity, 0.5f, 4.f, "%.2f");
    SliderFloat("Fall speed",  &m_FallSpeed,   0.01f, 0.2f, "%.3f");
    SliderFloat("Bar gap",     &m_BarGap,      0.f, 8.f, "%.1f px");
    SliderFloat("Opacity",     &m_Opacity,     0.0f, 1.f, "%.2f");
    Checkbox("Peak markers", &m_ShowPeak);

    Separator();
    if (Button(ICON_FK_REFRESH " Refresh audio device"))
        m_NeedsRestart = true;
    if (IsItemHovered())
        SetTooltip("Restarts the audio capture thread.\nUse this if the visualizer froze after changing audio devices.");
}

void AudioVisModule::Render()
{
}

void AudioVisModule::SettingsInit(SettingMgr& Settings)
{
    Settings["AudioVis"]["Enable"].GetAsBool (&Enabled);
    Settings["AudioVis"]["NumBars"].GetAsInt (&m_NumBars);
    Settings["AudioVis"]["Sensitivity"].GetAsFloat(&m_Sensitivity);
    Settings["AudioVis"]["FallSpeed"].GetAsFloat  (&m_FallSpeed);
    Settings["AudioVis"]["BarGap"].GetAsFloat      (&m_BarGap);
    Settings["AudioVis"]["Opacity"].GetAsFloat     (&m_Opacity);
    Settings["AudioVis"]["ShowPeak"].GetAsBool     (&m_ShowPeak);
    Settings["AudioVis"]["ColorScheme"].GetAsInt   (&m_ColorScheme);
    Settings["AudioVis"]["SolidR"].GetAsFloat(&m_SolidColor[0]);
    Settings["AudioVis"]["SolidG"].GetAsFloat(&m_SolidColor[1]);
    Settings["AudioVis"]["SolidB"].GetAsFloat(&m_SolidColor[2]);
    Settings["AudioVis"]["Grad1R"].GetAsFloat(&m_GradColor1[0]);
    Settings["AudioVis"]["Grad1G"].GetAsFloat(&m_GradColor1[1]);
    Settings["AudioVis"]["Grad1B"].GetAsFloat(&m_GradColor1[2]);
    Settings["AudioVis"]["Grad2R"].GetAsFloat(&m_GradColor2[0]);
    Settings["AudioVis"]["Grad2G"].GetAsFloat(&m_GradColor2[1]);
    Settings["AudioVis"]["Grad2B"].GetAsFloat(&m_GradColor2[2]);

    m_NumBars = std::max(8, std::min(m_NumBars, kMaxBars));

    float px = 0.f, py = 0.f;
    Settings["AudioVis"]["WndPosX"].GetAsFloat(&px);
    Settings["AudioVis"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }
}

void AudioVisModule::SettingsSave(SettingMgr& Settings)
{
    Settings["AudioVis"]["Enable"].Set(Enabled);
    Settings["AudioVis"]["NumBars"].Set(m_NumBars);
    Settings["AudioVis"]["Sensitivity"].Set(m_Sensitivity);
    Settings["AudioVis"]["FallSpeed"].Set(m_FallSpeed);
    Settings["AudioVis"]["BarGap"].Set(m_BarGap);
    Settings["AudioVis"]["Opacity"].Set(m_Opacity);
    Settings["AudioVis"]["ShowPeak"].Set(m_ShowPeak);
    Settings["AudioVis"]["ColorScheme"].Set(m_ColorScheme);
    Settings["AudioVis"]["SolidR"].Set(m_SolidColor[0]);
    Settings["AudioVis"]["SolidG"].Set(m_SolidColor[1]);
    Settings["AudioVis"]["SolidB"].Set(m_SolidColor[2]);
    Settings["AudioVis"]["Grad1R"].Set(m_GradColor1[0]);
    Settings["AudioVis"]["Grad1G"].Set(m_GradColor1[1]);
    Settings["AudioVis"]["Grad1B"].Set(m_GradColor1[2]);
    Settings["AudioVis"]["Grad2R"].Set(m_GradColor2[0]);
    Settings["AudioVis"]["Grad2G"].Set(m_GradColor2[1]);
    Settings["AudioVis"]["Grad2B"].Set(m_GradColor2[2]);
    Settings["AudioVis"]["WndPosX"].Set(m_WndPos.x);
    Settings["AudioVis"]["WndPosY"].Set(m_WndPos.y);
}
