#pragma once

#include "../../IModule.h"
#include <chrono>
#include <string>
#include <thread>
#include <mutex>
#include <atomic>

class RandomizerModule; // for the "Open In Browser" TMX button - see .cpp

// Discord Rich Presence via a direct connection to Discord's local IPC pipe
// (\\.\pipe\discord-ipc-0..9), the same protocol Discord's own SDKs ultimately talk over. This
// is implemented by hand here rather than bundling Discord's GameSDK, because the GameSDK's
// last-ever released build (frozen since 2022 - Discord discontinued it) has no support for
// Rich Presence buttons at all, while the underlying IPC protocol does. This also drops the
// discord_game_sdk.dll runtime dependency entirely - just a plain Windows named pipe.
class TwinkDiscordRPModule : public IModule
{
public:
    TwinkDiscordRPModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled, RandomizerModule* Randomizer)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "Discord Rich Presence";
        this->FancyName       = "Discord Rich Presence";
        this->Category        = "Integrations";
        this->m_Randomizer    = Randomizer;
    }
    ~TwinkDiscordRPModule();

    virtual void Render()         override {}
    virtual void RenderAnyways()  override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }

private:
    RandomizerModule* m_Randomizer = nullptr;

    void* m_Pipe      = nullptr; // HANDLE - kept as void* so <Windows.h> doesn't leak into this header
    bool  m_Connected = false;
    long long m_StartTimestampSec = 0;

    std::chrono::steady_clock::time_point m_LastUpdate{};
    std::chrono::steady_clock::time_point m_LastConnectAttempt{};
    unsigned long long m_NonceCounter = 0;

    // Background TMX name-search for non-RMC maps: looks up tmnf.exchange AND tmuf.exchange (not
    // just whichever the current game build is) by map name, so a Nations map that happens to
    // also be listed on the United site (or vice versa) still gets a correct exact-track link.
    // Only re-runs when the map name actually changes - a plain "$0"-per-frame HTTP lookup would
    // be wasteful, and TMX search results don't change mid-session anyway.
    std::thread       m_LookupThread;
    std::mutex        m_LookupMutex;
    std::atomic<bool> m_LookupResultReady{ false };
    std::atomic<bool> m_LookupInProgress{ false }; // set for the thread's whole lifetime - see StartTmxLookup()
    std::string       m_LookupResultUrl;      // guarded by m_LookupMutex until picked up
    std::string       m_LookupResultMapName;  // guarded by m_LookupMutex - which map the result is for
    std::string       m_LookupForMapName;     // main-thread only: which map we last started a lookup for
    std::string       m_CurrentFallbackUrl;   // main-thread only: current button URL for the fallback case

    void StartTmxLookup(const std::string& mapName);
    void TmxLookupWorker(std::string mapName);

    bool ConnectPipe();
    void DisconnectPipe();
    bool SendFrame(unsigned int opcode, const std::string& json);
    void UpdatePresence();

    // Presence text customization - each is a plain text field, empty by default. An empty field
    // means "use the built-in default" (see the kDefault* constants in the .cpp), so a user who
    // never touches this section gets exactly today's fixed strings, unchanged. "{map_name}"
    // in the two details templates is replaced with the current (formatting-stripped) map name.
    char m_TemplateMenusBuf[64]     = "";
    char m_TemplateRmcBuf[64]       = "";
    char m_TemplateMapBuf[64]       = "";
    char m_TemplateMapServerBuf[64] = ""; // used instead of m_TemplateMapBuf while actually on a server
    char m_TemplateDetailsBuf[128]  = "";
    char m_TemplateOfficialBuf[128] = "";
};
