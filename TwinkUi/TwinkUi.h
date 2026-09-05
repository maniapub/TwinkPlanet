#pragma once
#pragma execution_character_set("utf-8")

#include <string>
#include <format>
#include <iomanip>
#include <sstream>
#include <vector>
#include <chrono>
#include <atomic>
#include <thread>

#include "../Version.h"
#include "../SettingMgr/SettingMgr.h"
#include "../TwinkTrackmania/TwinkTrackmania.h"
#include "../TwinkLogs.h"
#include "../TwinkLuaMgr/TwinkLuaMgr.h"
#include "../TwinkIo/TwinkIo.h"
#include "../TwinkToast/TwinkToast.h"

#include "../imgui-dx9/imgui.h"
#include "../GlyphTable/IconsForkAwesome.h"
#include "../GlyphTable/IconsKenney.h"

#include "../Modules/About/About.h"
#include "../Modules/AlwaysOfficial/AlwaysOfficial.h"
#include "../Modules/AudioVis/AudioVis.h"
#include "../Modules/Blahaj/Blahaj.h"
#include "../Modules/CheckpointCounter/CheckpointCounter.h"
#include "../Modules/ClockSessionTimer/ClockSessionTimer.h"
#include "../Modules/Colors/Colors.h"
#include "../Modules/CopperTracker/CopperTracker.h"
#include "../Modules/Dashboard/DashboardInputs.h"
#include "../Modules/Dashboard/DashboardTacho.h"
#include "../Modules/Dashboard/DashboardGears.h"
#include "../Modules/DownloadServerMaps/DownloadServerMaps.h"
#include "../Modules/Fireworks/Fireworks.h"
#include "../Modules/GamingMode/GamingMode.h"
#include "../Modules/Image/Image.h"
#include "../Modules/LocalReplays/LocalReplays.h"
#include "../Modules/LSD/LSD.h"
#include "../Modules/MediaControls/MediaControls.h"
#include "../Modules/NoDistanceFog/NoDistanceFog.h"
#include "../Modules/TmxBrowser/TmxBrowser.h"
#include "../Modules/PerformanceOverlay/PerformanceOverlay.h"
#include "../Modules/Randomizer/Randomizer.h"
#include "../Modules/TwinkDiscordRP/TwinkDiscordRP.h"
#include "../Modules/WheelIndicators/WheelIndicators.h"
#include "../Modules/GhostEditor/GhostEditor.h"
#include "../Modules/GrindingStats/GrindingStats.h"
#include "../Modules/Medals/Medals.h"
#include "../Modules/SplitSpeeds/SplitSpeeds.h"
#include "../Modules/Tweaker/Tweaker.h"
#include "../Modules/Telemetry/Telemetry.h"
#include "../Modules/MapValidator/MapValidator.h"
#ifdef BUILD_DEBUG
#include "../Modules/PlayerInfo/PlayerInfo.h"
#endif
#include "../Modules/LuaConsole/LuaConsole.h"

#include <d3d9.h>

#include "../kiero/kiero.h"
#include "../kiero/minhook/include/MinHook.h"

using WNDPROC = LRESULT(CALLBACK*)(HWND, UINT, WPARAM, LPARAM);
using ResetFn = HRESULT(APIENTRY*)(LPDIRECT3DDEVICE9 pDevice, D3DPRESENT_PARAMETERS* pPresentationParameters);
using PresentFn = long(__stdcall*)(LPDIRECT3DDEVICE9 pDevice, LPVOID, LPVOID, HWND, LPVOID);

extern const char* g_FontNames[3];
extern const char* g_ThemeNames[2];

class TwinkUi
{
public:
    Versioning Versions;
    TwinkTrackmania TrackmaniaMgr;
    TwinkLogs Logger;
    TwinkIo* IoMgr;
    TwinkLuaMgr* LuaMgr;

    ImFont* FontMono = nullptr;
    ImFont* FontBricolageGrotesque = nullptr;
    ImFont* FontDroidSans = nullptr;
    ImFont* SelectedFont = FontDroidSans;
    size_t FontIdx = 2;
    std::string FontName = "DroidSans";

    size_t ThemeIdx = 1;
    std::string ThemeName = "Openplanet";

    float UiScale = 1.3f;

    bool DoRender = true;
    bool Initialized = false;
    bool ForceModulesNoRender = false;

    bool WantFullscreenWindowed = false;
    bool FullscreenWindowed = false;
    RECT WindowedRect = { 0, 0, 0, 0 };

    ResetFn oReset = NULL;
    PresentFn oPresent = NULL;
    WNDPROC oWndProc = NULL;
    HWND Window = NULL;

    bool EnableSettings = false;
    bool EnableImGuiDemo = false;

    std::vector<IModule*> Modules = {};
    AboutModule* AboutMod = nullptr;
    BlahajModule* BlahajMod = nullptr;
    LSDModule* LSDMod = nullptr;
    CopperTrackerModule* CopperTrackerMod = nullptr;
    AudioVisModule* AudioVisMod = nullptr;
    ClockSessionTimerModule* ClockSessionTimerMod = nullptr;
    ImageModule* ImageMod = nullptr;
    PerformanceOverlayModule* PerformanceOverlayMod = nullptr;
    RandomizerModule* RandomizerMod = nullptr;
    TwinkDiscordRPModule* TwinkDiscordRPMod = nullptr;
    WheelIndicatorsModule* WheelIndicatorsMod = nullptr;
    MediaControlsModule* MediaControlsMod = nullptr;
    FireworksModule* FireworksMod = nullptr;
    GamingModeModule* GamingModeMod = nullptr;
    ColorsModule* ColorsMod = nullptr;
    NoDistanceFogModule* NoDistanceFogMod = nullptr;
    TmxBrowserModule* TmxBrowserMod = nullptr;
    LocalReplaysModule* LocalReplaysMod = nullptr;

    kiero::Status::Enum DX9HookStatus = kiero::Status::UnknownError;

    SettingMgr Settings;

    size_t ActiveModuleIdx = 0;
    bool IsTwinkieSettingsOpen = true;

    bool LuaModulesLoaded = false;

    std::chrono::system_clock::time_point LastAutosaveTime = std::chrono::system_clock::now();
    float AutosaveIntervalMinutes = 2.f;

    // Positioning lines (centre crosshair + rule-of-thirds guides, with drag-to-snap)
    bool EnablePositioningLines = false;

    // Config system: named, saveable/loadable/shareable snapshots of the whole settings.ini
    char                     m_ConfigNameBuf[128]   = {};
    char                     m_ConfigImportUrl[512] = {};
    std::string              m_SelectedConfig;
    std::vector<std::string> m_ConfigFiles;
    std::string              m_ConfigActionStatus;
    std::string              m_ConfigImportStatus;
    std::atomic<bool>        m_ConfigImportDone{ false };
    std::thread              m_ConfigImportThread;

    TwinkToast& Toast = TwinkToast::Get();

    void SettingsInit();
    void SettingsSave();

    TwinkUi();
    ~TwinkUi();
    void PatchFullscreenWindowed(HWND WindowHandle);
    void InitFonts(ImGuiIO& ImIo);
    void SetupTwinkieImGuiStyle();
    void SetupOpenplanetImGuiStyle();

    void Render();
    void RenderSettings();
    void RenderAnyways();

    // Positioning helpers
    void SnapOverlaysToGuideLines();

    // Config helpers
    std::string GetConfigsDir() const;
    void        RefreshConfigList();
    void        SaveConfig(const std::string& name);
    void        LoadConfig(const std::string& name);
    void        DeleteConfig(const std::string& name);
    void        ExportConfigToClipboard(const std::string& name);
    void        ImportConfigFromURL(const std::string& url, const std::string& destName);
};