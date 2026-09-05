#define NOMINMAX
#include "MediaControls.h"
#include <cmath>

#pragma comment(lib, "ole32.lib")

// WinRT for SMTC (Windows 10 SDK 17134+)
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Control.h>
#pragma comment(lib, "windowsapp.lib")

// =============================================================================
// Audio device helpers
// =============================================================================

void MediaControlsModule::InitAudio()
{
    IMMDeviceEnumerator* pEnum = nullptr;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                __uuidof(IMMDeviceEnumerator), (void**)&pEnum)) || !pEnum)
        return;

    IMMDevice* pDevice = nullptr;
    pEnum->GetDefaultAudioEndpoint(eRender, eConsole, &pDevice);
    pEnum->Release();
    if (!pDevice) return;

    pDevice->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                      (void**)&m_VolumeEndpoint);
    pDevice->Release();

    if (m_VolumeEndpoint) {
        m_VolumeEndpoint->GetMasterVolumeLevelScalar(&m_Volume);
        BOOL muted = FALSE;
        m_VolumeEndpoint->GetMute(&muted);
        m_IsMuted = (muted != FALSE);
    }
}

void MediaControlsModule::RefreshAudio()
{
    if (m_VolumeEndpoint) { m_VolumeEndpoint->Release(); m_VolumeEndpoint = nullptr; }
    InitAudio();
}

// =============================================================================
// SMTC polling thread (WinRT)
// =============================================================================

void MediaControlsModule::StartSMTCThread()
{
    m_SMTCRunning = true;
    m_SMTCThread = std::thread([this]() {
        try {
            winrt::init_apartment();
            m_SMTCAvail = true;
        } catch (...) {
            m_SMTCAvail = false;
            return;
        }

        int  emptyCount    = 0;
        int  pollCount     = 0;
        int  graceCount    = 0;  // polls to ignore after a manager refresh
        static constexpr int kEmptyThreshold  = 20;  // clear after 20 consecutive empty results (~15s)
        static constexpr int kManagerRefresh  = 60;  // re-request manager every 60 polls (~45s)
        static constexpr int kGracePeriod     = 4;   // polls to skip counting after manager refresh

        using namespace winrt::Windows::Media::Control;
        GlobalSystemMediaTransportControlsSessionManager mgr{ nullptr };

        // Returns true for garbage placeholder titles (app literally set title to "?????????")
        auto isGarbage = [](const std::string& s) -> bool {
            if (s.empty()) return true;
            int q = 0;
            for (char c : s) if (c == '?') q++;
            return q >= (int)s.size() * 7 / 10;
        };

        while (m_SMTCRunning.load()) {
            try {
                // Periodically refresh the session manager to avoid stale sessions
                if (!mgr || pollCount % kManagerRefresh == 0) {
                    mgr = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
                    graceCount = kGracePeriod;  // give new manager time to settle
                }

                pollCount++;

                // Don't count empties during grace period after a refresh
                if (graceCount > 0) { graceCount--; Sleep(750); continue; }

                auto session = mgr ? mgr.GetCurrentSession() : nullptr;

                if (session) {
                    auto props  = session.TryGetMediaPropertiesAsync().get();
                    auto pb     = session.GetPlaybackInfo();

                    std::string title  = winrt::to_string(props.Title());
                    std::string artist = winrt::to_string(props.Artist());
                    bool playing = (pb.PlaybackStatus() ==
                        GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing);

                    if (!title.empty() && !isGarbage(title)) {
                        emptyCount = 0;
                        std::lock_guard<std::mutex> lk(m_Mutex);
                        m_Title     = std::move(title);
                        m_Artist    = std::move(artist);
                        m_IsPlaying = playing;
                    } else {
                        // No title or garbage placeholder — debounce before clearing
                        if (++emptyCount >= kEmptyThreshold) {
                            std::lock_guard<std::mutex> lk(m_Mutex);
                            m_Title.clear();
                            m_Artist.clear();
                            m_IsPlaying = false;
                        }
                    }
                } else {
                    if (++emptyCount >= kEmptyThreshold) {
                        std::lock_guard<std::mutex> lk(m_Mutex);
                        m_Title.clear();
                        m_Artist.clear();
                        m_IsPlaying = false;
                    }
                }
            } catch (...) {
                // On exception, force a manager refresh next iteration
                mgr = nullptr;
                graceCount = 0;
                if (++emptyCount >= kEmptyThreshold) {
                    std::lock_guard<std::mutex> lk(m_Mutex);
                    m_Title.clear();
                    m_Artist.clear();
                }
            }

            Sleep(750);
        }

        try { winrt::uninit_apartment(); } catch (...) {}
    });
}

// =============================================================================
// RenderAnyways – overlay, always visible regardless of F3
// =============================================================================

void MediaControlsModule::RenderAnyways()
{
    if (!Enabled) return;

    // Sync volume from system each frame so external changes are reflected
    if (m_VolumeEndpoint) {
        m_VolumeEndpoint->GetMasterVolumeLevelScalar(&m_Volume);
        BOOL muted = FALSE;
        m_VolumeEndpoint->GetMute(&muted);
        m_IsMuted = (muted != FALSE);
    }

    // Snapshot SMTC data under lock
    std::string title, artist;
    bool isPlaying;
    {
        std::lock_guard<std::mutex> lk(m_Mutex);
        title     = m_Title;
        artist    = m_Artist;
        isPlaying = m_IsPlaying;
    }

    bool menuVisible = *UiRenderEnabled;

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse  |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (!menuVisible)
        flags |= ImGuiWindowFlags_NoMove;

    if (m_WndPosSet) { ImGui::SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
    else { ImGui::SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }

    ImGui::SetNextWindowBgAlpha(0.80f);
    if (!ImGui::Begin("##MediaControls", nullptr, flags)) { ImGui::End(); return; }
    m_WndPos = ImGui::GetWindowPos();

    // Track info
    if (!title.empty()) {
        ImGui::TextUnformatted(title.c_str());
        if (!artist.empty())
            ImGui::TextDisabled("%s", artist.c_str());
    } else {
        ImGui::TextDisabled("No media playing");
    }

    ImGui::Spacing();

    // ── Playback controls ────────────────────────────────────────────────────
    if (ImGui::Button(ICON_FK_BACKWARD)) {
        keybd_event(VK_MEDIA_PREV_TRACK, 0, 0, 0);
        keybd_event(VK_MEDIA_PREV_TRACK, 0, KEYEVENTF_KEYUP, 0);
    }
    ImGui::SameLine();
    if (ImGui::Button(isPlaying ? ICON_FK_PAUSE : ICON_FK_PLAY)) {
        keybd_event(VK_MEDIA_PLAY_PAUSE, 0, 0, 0);
        keybd_event(VK_MEDIA_PLAY_PAUSE, 0, KEYEVENTF_KEYUP, 0);
    }
    ImGui::SameLine();
    if (ImGui::Button(ICON_FK_FORWARD)) {
        keybd_event(VK_MEDIA_NEXT_TRACK, 0, 0, 0);
        keybd_event(VK_MEDIA_NEXT_TRACK, 0, KEYEVENTF_KEYUP, 0);
    }

    ImGui::SameLine();
    ImGui::Spacing();
    ImGui::SameLine();

    // ── Volume ───────────────────────────────────────────────────────────────
    const char* volIcon = m_IsMuted || m_Volume < 0.01f
                          ? ICON_FK_VOLUME_OFF
                          : m_Volume < 0.45f
                            ? ICON_FK_VOLUME_DOWN
                            : ICON_FK_VOLUME_UP;

    if (ImGui::Button(volIcon) && m_VolumeEndpoint) {
        m_IsMuted = !m_IsMuted;
        m_VolumeEndpoint->SetMute(m_IsMuted ? TRUE : FALSE, nullptr);
    }
    ImGui::SameLine();

    float volPct = m_Volume * 100.f;
    ImGui::SetNextItemWidth(100.f);
    if (ImGui::SliderFloat("##vol", &volPct, 0.f, 100.f, "%.0f%%")) {
        m_Volume = volPct / 100.f;
        if (m_VolumeEndpoint)
            m_VolumeEndpoint->SetMasterVolumeLevelScalar(m_Volume, nullptr);
    }

    ImGui::End();
}

// =============================================================================
// RenderMenuItem / RenderSettings
// =============================================================================

void MediaControlsModule::RenderMenuItem()
{
    if (ImGui::MenuItem(ICON_FK_MUSIC " Media Controls", "", Enabled))
        Enabled = !Enabled;
}

void MediaControlsModule::RenderSettings()
{
    if (!m_SMTCAvail)
        ImGui::TextDisabled("Track info unavailable (WinRT not initialized).");

    if (!m_VolumeEndpoint)
        ImGui::TextDisabled("Volume control unavailable (no audio device found).");

    ImGui::Spacing();
    if (ImGui::Button(ICON_FK_REFRESH " Refresh audio device"))
        RefreshAudio();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Re-initializes the volume endpoint.\nUse this after changing your default audio device.");
}

// =============================================================================
// Settings
// =============================================================================

void MediaControlsModule::SettingsInit(SettingMgr& Settings)
{
    Settings["MediaControls"]["Enable"].GetAsBool(&Enabled);
    float px = 0.f, py = 0.f;
    Settings["MediaControls"]["WndPosX"].GetAsFloat(&px);
    Settings["MediaControls"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }
}

void MediaControlsModule::SettingsSave(SettingMgr& Settings)
{
    Settings["MediaControls"]["Enable"].Set(Enabled);
    Settings["MediaControls"]["WndPosX"].Set(m_WndPos.x);
    Settings["MediaControls"]["WndPosY"].Set(m_WndPos.y);
}
