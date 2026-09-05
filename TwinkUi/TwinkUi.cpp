#include "TwinkUi.h"
#include "../imgui-dx9/imgui_internal.h"
#include "../Utils.h"
#include "../Resource.h"
#include <shellapi.h>
#include <winhttp.h>
#include <fstream>
#include <algorithm>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "winhttp.lib")

void TwinkUi::SettingsInit()
{
    for (IModule* Module : Modules)
    {
        Module->SettingsInit(Settings);
    }
    
    Settings["Twinkie"]["UI Scale"].GetAsFloat(&UiScale);
    Settings["Twinkie"]["AutPosave interval (minutes)"].GetAsFloat(&AutosaveIntervalMinutes);
    Settings["Twinkie"]["Font"].GetAsString(&FontName);
    // Theme is no longer user-selectable - deliberately not loading it, so ThemeIdx/ThemeName stay
    // at their Openplanet defaults regardless of what an older settings.ini might have saved.
    Settings["Twinkie"]["Fullscreen windowed"].GetAsBool(&WantFullscreenWindowed);
    Settings["Twinkie"]["Positioning Lines"].GetAsBool(&EnablePositioningLines);

    FontIdx = FontName == "BricolageGrotesque" ? 1 : FontName == "DroidSans" ? 2 : FontName == "ComicNeue" ? 3 : 0;

    RefreshConfigList();

    if (Settings.Status != 0)
    {
        Logger.PrintErrorArgs("Could not load settings with reason {}", Settings.Status);
    }
}

void TwinkUi::SettingsSave()
{
    for (IModule* Module : Modules)
    {
        Module->SettingsSave(Settings);
    }

    Settings["Twinkie"]["UI Scale"].Set(UiScale);
    Settings["Twinkie"]["Autosave interval (minutes)"].Set(AutosaveIntervalMinutes);
    Settings["Twinkie"]["Font"].Set(FontName);
    Settings["Twinkie"]["Fullscreen windowed"].Set(WantFullscreenWindowed);
    Settings["Twinkie"]["Positioning Lines"].Set(EnablePositioningLines);
}

TwinkUi::TwinkUi()
{
    IoMgr = new TwinkIo(TrackmaniaMgr);
    LuaMgr = new TwinkLuaMgr(&Logger);

    Logger.PrintInternal(":3c");
    Logger.PrintInternalArgs("Twinkie for TrackMania Forever. Version {}", Versions.TwinkieVer);

#ifdef BUILD_DEBUG
    Modules.push_back(new PlayerInfoModule(TrackmaniaMgr, Logger, &DoRender));
#endif
    Modules.push_back(new LuaConsoleModule(TrackmaniaMgr, Logger, &DoRender));
    AboutMod = new AboutModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(AboutMod);
    TmxBrowserMod = new TmxBrowserModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(TmxBrowserMod);
    //
    Modules.push_back(new DashboardInputsModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new DashboardTachometerModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new DashboardGearsModule(TrackmaniaMgr, Logger, &DoRender));
    //
    Modules.push_back(new CheckpointCounterModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new SplitSpeedsModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new GrindingStatsModule(TrackmaniaMgr, Logger, &DoRender));
    //
    Modules.push_back(new MedalsModule(TrackmaniaMgr, Logger, &DoRender));
    //
	Modules.push_back(new MapValidatorModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new GhostEditorModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new TweakerModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new AlwaysOfficialModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new TelemetryModule(TrackmaniaMgr, Logger, &DoRender));
    Modules.push_back(new DownloadServerMapsModule(TrackmaniaMgr, Logger, &DoRender));
    //
    BlahajMod = new BlahajModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(BlahajMod);
    LSDMod = new LSDModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(LSDMod);
    CopperTrackerMod = new CopperTrackerModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(CopperTrackerMod);
    AudioVisMod = new AudioVisModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(AudioVisMod);
    ClockSessionTimerMod = new ClockSessionTimerModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(ClockSessionTimerMod);
    ImageMod = new ImageModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(ImageMod);
    PerformanceOverlayMod = new PerformanceOverlayModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(PerformanceOverlayMod);
    WheelIndicatorsMod = new WheelIndicatorsModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(WheelIndicatorsMod);
    RandomizerMod = new RandomizerModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(RandomizerMod);
    TwinkDiscordRPMod = new TwinkDiscordRPModule(TrackmaniaMgr, Logger, &DoRender, RandomizerMod);
    Modules.push_back(TwinkDiscordRPMod);
    MediaControlsMod = new MediaControlsModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(MediaControlsMod);
    FireworksMod = new FireworksModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(FireworksMod);
    GamingModeMod = new GamingModeModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(GamingModeMod);
    ColorsMod = new ColorsModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(ColorsMod);
    NoDistanceFogMod = new NoDistanceFogModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(NoDistanceFogMod);
    LocalReplaysMod = new LocalReplaysModule(TrackmaniaMgr, Logger, &DoRender);
    Modules.push_back(LocalReplaysMod);

    Logger.PrintInternalArgs("{} C++ module{} initialized.", Modules.size(), Modules.size() == 1 ? "" : "s");

    SettingsInit();

    if (TrackmaniaMgr.TMInterfaceLoaded)
    {
        Logger.PrintWarn("TMInterface was found, some modules' features will be disabled");
    }
}

TwinkUi::~TwinkUi()
{
    if (m_ConfigImportThread.joinable()) m_ConfigImportThread.join();

    SettingsSave();

    Settings.Save();

    for (IModule* Module : Modules)
    {
        delete Module;
        Module = nullptr;
    }

    delete IoMgr;
    IoMgr = nullptr;

    delete LuaMgr;
    LuaMgr = nullptr;

    CloseLuaAll();
}

void TwinkUi::PatchFullscreenWindowed(HWND WindowHandle)
{
#define EnableFullscreenPatch true
#define DisableFullscreenPatch false
    if (WantFullscreenWindowed and FullscreenWindowed) return;
    if (!WantFullscreenWindowed and !FullscreenWindowed) return;

    if (WantFullscreenWindowed)
    {
        MONITORINFO MonitorInfo = { sizeof(MONITORINFO) };
        HMONITOR Monitor = MonitorFromWindow(WindowHandle, MONITOR_DEFAULTTOPRIMARY);
        GetWindowRect(WindowHandle, &WindowedRect);

        if (GetMonitorInfo(Monitor, &MonitorInfo))
        {
            RECT MonitorRect = MonitorInfo.rcMonitor;

            SetWindowLong(WindowHandle, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLong(WindowHandle, GWL_EXSTYLE, WS_EX_APPWINDOW);

            SetWindowPos(
                WindowHandle,
                HWND_TOP,
                MonitorRect.left,
                MonitorRect.top,
                MonitorRect.right - MonitorRect.left,
                MonitorRect.bottom - MonitorRect.top,
                SWP_FRAMECHANGED | SWP_NOOWNERZORDER
            );
        }

        int ScreenWidth = MonitorInfo.rcMonitor.right - MonitorInfo.rcMonitor.left;
        int ScreenHeight = MonitorInfo.rcMonitor.bottom - MonitorInfo.rcMonitor.top;

        TrackmaniaMgr.SetFullscreenWindowedResolution(EnableFullscreenPatch, ScreenWidth, ScreenHeight);

        FullscreenWindowed = true;
        return;
    }
    else if (!WantFullscreenWindowed)
    {
        SetWindowLong(WindowHandle, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
        SetWindowLong(WindowHandle, GWL_EXSTYLE, WS_EX_APPWINDOW | WS_EX_WINDOWEDGE);

        SetWindowPos(
            WindowHandle,
            HWND_NOTOPMOST,
            WindowedRect.left,
            WindowedRect.top,
            WindowedRect.right - WindowedRect.left,
            WindowedRect.bottom - WindowedRect.top,
            SWP_FRAMECHANGED | SWP_NOOWNERZORDER
        );

        ShowWindow(WindowHandle, SW_NORMAL);

        TrackmaniaMgr.SetFullscreenWindowedResolution(DisableFullscreenPatch, 512, 384);

        FullscreenWindowed = false;
        return;
    }
}

// Loads one font from an RCDATA resource baked into this DLL (see Twinkie.rc) instead of a file
// on disk - FontDataOwnedByAtlas is forced false since the returned pointer is a Win32 resource
// (valid for the module's whole lifetime), not something ImGui should ever try to free.
static ImFont* AddEmbeddedFont(ImFontAtlas* Atlas, int ResourceId, float SizePixels, ImFontConfig* Cfg)
{
    const unsigned char* Data = nullptr;
    size_t Size = 0;
    if (!LoadEmbeddedResource(ResourceId, &Data, &Size)) return nullptr;

    Cfg->FontDataOwnedByAtlas = false;
    return Atlas->AddFontFromMemoryTTF((void*)Data, (int)Size, SizePixels, Cfg);
}

void TwinkUi::InitFonts(ImGuiIO& ImIo)
{
    bool SkipIconsMono = false;
    bool SkipIconsBricolageGrotesque = false;
    bool SkipIconsDroidSans = false;

    ImFontConfig MonoCfg;
    MonoCfg.Flags |= ImFontFlags_NoLoadError;

    FontMono = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_CASCADIAMONO, 14.f * UiScale, &MonoCfg);

    if (FontMono)
    {
        Logger.PrintInternal("Font \"CascadiaMono\" initialized.");
    }
    else
    {
        Logger.PrintError("Font \"CascadiaMono\" not initialized.");
        SkipIconsMono = true;
    }

    if (!SkipIconsMono)
    {
        // Taken example from https://github.com/juliettef/IconFontCppHeaders?tab=readme-ov-file#example-code
        float MonoIconFontSize = (14.f * UiScale);

        ImFontConfig MonoIconCfg;
        MonoIconCfg.MergeMode = true;
        MonoIconCfg.PixelSnapH = true;
        MonoIconCfg.GlyphMinAdvanceX = MonoIconFontSize;
        MonoIconCfg.Flags |= ImFontFlags_NoLoadError;

        auto FontManiaIconsMono = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_MANIAICONS, MonoIconFontSize, &MonoIconCfg);

        if (FontManiaIconsMono)
        {
            Logger.PrintInternal("Font \"ManiaIcons\" initialized.");
        }
        else
        {
            Logger.PrintError("Font \"ManiaIcons\" not initialized.");
        }
    }

    ImFontConfig BricolageGrotesqueCfg;
    BricolageGrotesqueCfg.MergeMode = false;
    BricolageGrotesqueCfg.Flags |= ImFontFlags_NoLoadError;

    FontBricolageGrotesque = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_BRICOLAGEGROTESQUE, 14.f * UiScale, &BricolageGrotesqueCfg);

    if (FontBricolageGrotesque)
    {
        Logger.PrintInternal("Font \"BricolageGrotesque\" initialized.");
    }
    else
    {
        Logger.PrintError("Font \"BricolageGrotesque\" not initialized.");
        SkipIconsBricolageGrotesque = true;
    }

    if (!SkipIconsBricolageGrotesque)
    {
        // Taken example from https://github.com/juliettef/IconFontCppHeaders?tab=readme-ov-file#example-code
        float BricolageGrotesqueIconFontSize = (14.f * UiScale);

        ImFontConfig BricolageGrotesqueIconCfg;
        BricolageGrotesqueIconCfg.MergeMode = true;
        BricolageGrotesqueIconCfg.PixelSnapH = true;
        BricolageGrotesqueIconCfg.GlyphMinAdvanceX = BricolageGrotesqueIconFontSize;
        BricolageGrotesqueIconCfg.Flags |= ImFontFlags_NoLoadError;

        auto FontManiaIconsBricolageGrotesque = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_MANIAICONS, BricolageGrotesqueIconFontSize, &BricolageGrotesqueIconCfg);

        if (FontManiaIconsBricolageGrotesque)
        {
            Logger.PrintInternal("Font \"ManiaIcons\" initialized.");
        }
        else
        {
            Logger.PrintError("Font \"ManiaIcons\" not initialized.");
        }
    }

    ImFontConfig DroidSansCfg;
    DroidSansCfg.MergeMode = false;
    DroidSansCfg.Flags |= ImFontFlags_NoLoadError;

    FontDroidSans = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_DROIDSANS, 14.f * UiScale, &DroidSansCfg);

    if (FontDroidSans)
    {
        Logger.PrintInternal("Font \"DroidSans\" initialized.");
    }
    else
    {
        Logger.PrintError("Font \"DroidSans\" not initialized.");
        SkipIconsDroidSans = true;
    }

    if (!SkipIconsDroidSans)
    {
        // Taken example from https://github.com/juliettef/IconFontCppHeaders?tab=readme-ov-file#example-code
        float DroidSansIconFontSize = (14.f * UiScale);

        ImFontConfig DroidSansIconCfg;
        DroidSansIconCfg.MergeMode = true;
        DroidSansIconCfg.PixelSnapH = true;
        DroidSansIconCfg.GlyphMinAdvanceX = DroidSansIconFontSize;
        DroidSansIconCfg.Flags |= ImFontFlags_NoLoadError;

        auto FontManiaIconsDroidSans = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_MANIAICONS, DroidSansIconFontSize, &DroidSansIconCfg);

        if (FontManiaIconsDroidSans)
        {
            Logger.PrintInternal("Font \"ManiaIcons\" initialized.");
        }
        else
        {
            Logger.PrintError("Font \"ManiaIcons\" not initialized.");
        }
    }

    bool SkipIconsComicNeue = false;

    ImFontConfig ComicNeueCfg;
    ComicNeueCfg.MergeMode = false;
    ComicNeueCfg.Flags |= ImFontFlags_NoLoadError;

    FontComicNeue = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_COMICNEUE, 14.f * UiScale, &ComicNeueCfg);

    if (FontComicNeue)
    {
        Logger.PrintInternal("Font \"ComicNeue\" initialized.");
    }
    else
    {
        Logger.PrintError("Font \"ComicNeue\" not initialized.");
        SkipIconsComicNeue = true;
    }

    if (!SkipIconsComicNeue)
    {
        // Taken example from https://github.com/juliettef/IconFontCppHeaders?tab=readme-ov-file#example-code
        float ComicNeueIconFontSize = (14.f * UiScale);

        ImFontConfig ComicNeueIconCfg;
        ComicNeueIconCfg.MergeMode = true;
        ComicNeueIconCfg.PixelSnapH = true;
        ComicNeueIconCfg.GlyphMinAdvanceX = ComicNeueIconFontSize;
        ComicNeueIconCfg.Flags |= ImFontFlags_NoLoadError;

        auto FontManiaIconsComicNeue = AddEmbeddedFont(ImIo.Fonts, IDR_FONT_MANIAICONS, ComicNeueIconFontSize, &ComicNeueIconCfg);

        if (FontManiaIconsComicNeue)
        {
            Logger.PrintInternal("Font \"ManiaIcons\" initialized.");
        }
        else
        {
            Logger.PrintError("Font \"ManiaIcons\" not initialized.");
        }
    }
}

void TwinkUi::SetupTwinkieImGuiStyle()
{
    // Soft Cherry style by Patitotective from ImThemes, twinkified
    ImGuiStyle& style = ImGui::GetStyle();

    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.4000000059604645f;
    style.WindowPadding = ImVec2(10.0f, 10.0f);
    style.WindowRounding = 4.0f;
    style.WindowBorderSize = 0.0f;
    style.WindowMinSize = ImVec2(50.0f, 50.0f);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);
    style.WindowMenuButtonPosition = ImGuiDir_Left;
    style.ChildRounding = 0.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 1.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(5.0f, 3.0f);
    style.FrameRounding = 3.0f;
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(6.0f, 6.0f);
    style.ItemInnerSpacing = ImVec2(3.0f, 2.0f);
    style.CellPadding = ImVec2(3.0f, 3.0f);
    style.IndentSpacing = 6.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 13.0f;
    style.ScrollbarRounding = 16.0f;
    style.GrabMinSize = 8.0f;
    style.GrabRounding = 4.0f;
    style.TabRounding = 4.0f;
    style.TabBorderSize = 1.0f;
    // style.TabMinWidthForCloseButton = 0.0f; // Removed in 1.92.1
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    style.Colors[ImGuiCol_Text] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.5215686559677124f, 0.5490196347236633f, 0.5333333611488342f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.1294117718935013f, 0.1372549086809158f, 0.168627455830574f, 0.8f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.1490196138620377f, 0.1568627506494522f, 0.1882352977991104f, 0.8f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.1372549086809158f, 0.1137254908680916f, 0.1333333402872086f, 1.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.168627455830574f, 0.1843137294054031f, 0.2313725501298904f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.2313725501298904f, 0.2000000029802322f, 0.2705882489681244f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.2000000029802322f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.239215686917305f, 0.239215686917305f, 0.2196078449487686f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.3882353007793427f, 0.3882353007793427f, 0.3725490272045135f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.6941176652908325f, 0.6941176652908325f, 0.686274528503418f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.6941176652908325f, 0.6941176652908325f, 0.686274528503418f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.658823549747467f, 0.1372549086809158f, 0.1764705926179886f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.7098039388656616f, 0.2196078449487686f, 0.2666666805744171f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.501960813999176f, 0.07450980693101883f, 0.2549019753932953f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.4274509847164154f, 0.4274509847164154f, 0.4980392158031464f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.09803921729326248f, 0.4000000059604645f, 0.7490196228027344f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.6509804129600525f, 0.1490196138620377f, 0.3450980484485626f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.1764705926179886f, 0.3490196168422699f, 0.5764706134796143f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_TabActive] = ImVec4(0.196078434586525f, 0.407843142747879f, 0.6784313917160034f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocused] = ImVec4(0.06666667014360428f, 0.1019607856869698f, 0.1450980454683304f, 1.0f);
    style.Colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.1333333402872086f, 0.2588235437870026f, 0.4235294163227081f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.8588235378265381f, 0.929411768913269f, 0.886274516582489f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.3098039329051971f, 0.7764706015586853f, 0.196078434586525f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.4549019634723663f, 0.196078434586525f, 0.2980392277240753f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.1882352977991104f, 0.1882352977991104f, 0.2000000029802322f, 1.0f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.3098039329051971f, 0.3098039329051971f, 0.3490196168422699f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.2274509817361832f, 0.2274509817361832f, 0.2470588237047195f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.0f, 0.0f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.3843137323856354f, 0.6274510025978088f, 0.9176470637321472f, 1.0f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.2588235437870026f, 0.5882353186607361f, 0.9764705896377563f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 1.0f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.800000011920929f, 0.800000011920929f, 0.800000011920929f, 0.300000011920929f);
}

void TwinkUi::SetupOpenplanetImGuiStyle()
{
    ImGuiStyle& style = ImGui::GetStyle();

    style.DisplayWindowPadding = { 16, 16 };
    style.DisplaySafeAreaPadding = { 0, 0 };
    style.WindowRounding = 3;
    style.ChildRounding = 2;
    style.FrameRounding = 3;
    style.WindowBorderSize = 0;

    style.ScrollbarSize = 14;
    style.ScrollbarRounding = 2;

    style.TabBarOverlineSize = 2;

    style.GrabRounding = 2;
    style.ItemSpacing = { 10, 6 };
    style.IndentSpacing = 22;
    style.FramePadding = { 7, 4 };
    style.Alpha = 1;

    style.Colors[ImGuiCol_Text] = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.4980392156862745f, 0.4980392156862745f, 0.4980392156862745f, 1.0f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.12549019607843137f, 0.12549019607843137f, 0.12549019607843137f, 0.9921568627450981f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(1.0f, 1.0f, 1.0f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10980392156862745f, 0.10980392156862745f, 0.10980392156862745f, 0.9921568627450981f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.792156862745098f, 0.792156862745098f, 0.792156862745098f, 0.0f);
    style.Colors[ImGuiCol_BorderShadow] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.09803921568627451f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.9372549019607843f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.4f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.6666666666666666f);
    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.10196078431372549f, 0.10196078431372549f, 0.10196078431372549f, 1.0f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.1568627450980392f, 0.1568627450980392f, 0.1568627450980392f, 1.0f);
    style.Colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.5098039215686274f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.1568627450980392f, 0.1568627450980392f, 0.1568627450980392f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.08235294117647059f, 0.08235294117647059f, 0.08235294117647059f, 0.5294117647058824f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.4f, 0.4f, 0.4f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.5098039215686274f, 0.5098039215686274f, 0.5098039215686274f, 1.0f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.6196078431372549f, 0.6196078431372549f, 0.6196078431372549f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.3411764705882353f, 0.403921568627451f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.4f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.2235294117647059f, 0.2980392156862745f, 0.6705882352941176f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.30980392156862746f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.8f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_Separator] = ImVec4(0.7294117647058823f, 0.7294117647058823f, 0.7294117647058823f, 1.0f);
    style.Colors[ImGuiCol_SeparatorHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.7764705882352941f);
    style.Colors[ImGuiCol_SeparatorActive] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.4980392156862745f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.6666666666666666f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.9490196078431372f);
    style.Colors[ImGuiCol_Tab] = ImVec4(0.25882352941176473f, 0.2980392156862745f, 0.6941176470588235f, 0.8588235294117647f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.8f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.2901960784313726f, 0.3411764705882353f, 0.807843137254902f, 1.0f);
    style.Colors[ImGuiCol_TabSelectedOverline] = ImVec4(0.2901960784313726f, 0.3411764705882353f, 0.807843137254902f, 1.0f);
    style.Colors[ImGuiCol_TabDimmed] = ImVec4(0.11372549019607843f, 0.12549019607843137f, 0.2196078431372549f, 0.9686274509803922f);
    style.Colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.2f, 0.23137254901960785f, 0.5254901960784314f, 1.0f);
    style.Colors[ImGuiCol_TabDimmedSelectedOverline] = ImVec4(0.2f, 0.23137254901960785f, 0.5254901960784314f, 1.0f);
    //style.Colors[ImGuiCol_DockingPreview] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.6980392156862745f);
    //style.Colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.9411764705882353f, 0.9411764705882353f, 0.9411764705882353f, 1.0f);
    style.Colors[ImGuiCol_PlotLines] = ImVec4(0.7294117647058823f, 0.7294117647058823f, 0.7294117647058823f, 1.0f);
    style.Colors[ImGuiCol_PlotLinesHovered] = ImVec4(1.0f, 0.6823529411764706f, 0.4117647058823529f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogram] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.8901960784313725f, 1.0f, 0.09803921568627451f, 1.0f);
    style.Colors[ImGuiCol_TableHeaderBg] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.6470588235294118f);
    style.Colors[ImGuiCol_TableBorderStrong] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 1.0f);
    style.Colors[ImGuiCol_TableBorderLight] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TableRowBg] = ImVec4(0.16862745098039217f, 0.16862745098039217f, 0.16862745098039217f, 0.6274509803921569f);
    style.Colors[ImGuiCol_TableRowBgAlt] = ImVec4(0.1411764705882353f, 0.1411764705882353f, 0.1411764705882353f, 0.6274509803921569f);
    style.Colors[ImGuiCol_TextLink] = ImVec4(0.2f, 0.4f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_TextSelectedBg] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 0.34901960784313724f);
    style.Colors[ImGuiCol_DragDropTarget] = ImVec4(0.6901960784313725f, 1.0f, 0.09803921568627451f, 0.8980392156862745f);
    style.Colors[ImGuiCol_NavHighlight] = ImVec4(0.33725490196078434f, 0.41568627450980394f, 1.0f, 1.0f);
    style.Colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.6980392156862745f);
    style.Colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.27450980392156865f, 0.27450980392156865f, 0.27450980392156865f, 0.2f);
    style.Colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.058823529411764705f, 0.058823529411764705f, 0.058823529411764705f, 0.8980392156862745f);
}

void TwinkUi::Render()
{
    using namespace ImGui;
    if (SelectedFont) PushFont(SelectedFont, 14.f * UiScale);

    for (IModule* Module : Modules)
    {
        if (ForceModulesNoRender) break;
        if (Module->Enabled)
        {
            Module->Render();
            if (!Module->Enabled)
                Toast.Push(Module->FancyName + ": OFF", 2.5f, ImVec4(1.f, 0.25f, 0.2f, 1.f));
        }
    }

    LuaMgr->RunModulesRender();

    if (BeginMainMenuBar()) {
        std::string DocsFolder = GetDocumentsFolder();
        std::string UserFolder = DocsFolder + "\\TwinkPlanet";
        std::string PluginsFolder = UserFolder + "\\LuaScripts";

        // Coloring just the heart glyph (leaving "TwinkPlanet" the normal color) needed an
        // overpaint trick that kept causing visual glitches (double heart, positioning slop) -
        // simpler and more robust to just color the whole label the same way.
        PushStyleColor(ImGuiCol_Text, ColorConvertFloat4ToU32({ 0.859f, 0.f, 0.431f, 1.f }));
        PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
        bool TwinkPlanetMenuOpen = BeginMenu(ICON_FK_HEART " TwinkPlanet");
        PopStyleColor();
        if (TwinkPlanetMenuOpen)
        {
            if (MenuItem(ICON_FK_COG " Settings", "", EnableSettings))
            {
                EnableSettings = !EnableSettings;
            }

            if (MenuItem(ICON_FK_COMMENT " Log", "", Logger.EnableLog))
            {
                Logger.EnableLog = !Logger.EnableLog;
            }

            Separator();

            if (MenuItem(ICON_FK_FOLDER_OPEN " Open user folder"))
            {
                ShellExecuteA(NULL, "explore", UserFolder.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }

            if (MenuItem(ICON_FK_FOLDER_OPEN " Open plugins folder"))
            {
                ShellExecuteA(NULL, "explore", PluginsFolder.c_str(), NULL, NULL, SW_SHOWNORMAL);
            }


            Separator();

            if (MenuItem(ICON_FK_WINDOWS " Fullscreen Windowed", "", FullscreenWindowed))
            {
                WantFullscreenWindowed = !WantFullscreenWindowed;
            }

            Separator();

            if (MenuItem(ICON_FK_POWER_OFF " Shutdown", ""))
            {
                TrackmaniaMgr.CallGbxAppExit();
            }
            if (IsItemHovered())
            {
                SetTooltip("Softly closes the game.");
            }

            if (MenuItem(ICON_FK_EXCLAMATION_TRIANGLE " Exit", ""))
            {
                Logger.PrintInternal("Terminating, as user commanded");
                free((void*)TrackmaniaMgr.GetTrackmania());
            }
            if (IsItemHovered())
            {
                SetTooltip(ICON_FK_EXCLAMATION_TRIANGLE " WARNING! This crashes the game.");
            }

            ImGui::EndMenu();
        }

        if (TmxBrowserMod and MenuItem(ICON_FK_SEARCH " TMX", "", TmxBrowserMod->Enabled))
        {
            TmxBrowserMod->Enabled = !TmxBrowserMod->Enabled;
        }

        if (BeginMenu(ICON_FK_SLIDERS " Modules"))
        {
            // Grouped by IModule::Category (set in each module's own constructor) instead of
            // whatever order they happened to be pushed into Modules in - "Other" is a catch-all
            // for anything left uncategorized, so a module never silently disappears from here.
            static const char* CategoryOrder[] = { "Dashboard", "Race Info", "Track Tools", "Utility", "Other" };

            for (const char* Category : CategoryOrder)
            {
                bool IsOtherBucket = (std::string(Category) == "Other");
                bool AnyInThisCategory = false;

                for (IModule* Module : Modules)
                {
                    bool IsCustomModule = (Module == BlahajMod or Module == LSDMod or Module == CopperTrackerMod
                        or Module == AudioVisMod or Module == ClockSessionTimerMod or Module == ImageMod
                        or Module == PerformanceOverlayMod or Module == WheelIndicatorsMod or Module == RandomizerMod
                        or Module == TwinkDiscordRPMod or Module == MediaControlsMod or Module == FireworksMod
                        or Module == GamingModeMod or Module == ColorsMod or Module == NoDistanceFogMod
                        or Module == LocalReplaysMod);
                    if (Module->IsDebug() or IsCustomModule or !Module->HasMenuEntry()) continue;

                    bool MatchesThisCategory = IsOtherBucket ? Module->Category.empty() : (Module->Category == Category);
                    if (!MatchesThisCategory) continue;

                    if (!AnyInThisCategory)
                    {
                        SeparatorText(Category);
                        AnyInThisCategory = true;
                    }

                    bool WasEnabled = Module->Enabled;
                    Module->RenderMenuItem();
                    if (Module->Enabled != WasEnabled)
                        Toast.Push(Module->FancyName + (Module->Enabled ? ": ON" : ": OFF"), 2.5f,
                            Module->Enabled ? ImVec4(0.2f, 1.f, 0.3f, 1.f) : ImVec4(1.f, 0.25f, 0.2f, 1.f));
                }
            }
            LuaMgr->RunModulesRenderMenuItem();
            ImGui::EndMenu();
        }
        if (BeginMenu(ICON_FK_HEART " Custom"))
        {
            // Grouped by IModule::Category same as the "Modules" menu above - "Other" is a
            // catch-all for anything left uncategorized, so a module never silently disappears.
            static const char* CustomCategoryOrder[] = { "Visual Effects", "Overlays", "Integrations", "Track Tools", "Other" };
            IModule* CustomModules[] = { BlahajMod, LSDMod, CopperTrackerMod, AudioVisMod, ClockSessionTimerMod,
                ImageMod, PerformanceOverlayMod, WheelIndicatorsMod, RandomizerMod, TwinkDiscordRPMod,
                MediaControlsMod, FireworksMod, GamingModeMod, ColorsMod, NoDistanceFogMod, LocalReplaysMod };

            for (const char* Category : CustomCategoryOrder)
            {
                bool IsOtherBucket = (std::string(Category) == "Other");
                bool AnyInThisCategory = false;

                for (IModule* Module : CustomModules)
                {
                    if (!Module or !Module->HasMenuEntry()) continue;

                    bool MatchesThisCategory = IsOtherBucket ? Module->Category.empty() : (Module->Category == Category);
                    if (!MatchesThisCategory) continue;

                    if (!AnyInThisCategory)
                    {
                        SeparatorText(Category);
                        AnyInThisCategory = true;
                    }

                    bool WasEnabled = Module->Enabled;
                    Module->RenderMenuItem();
                    if (Module->Enabled != WasEnabled)
                        Toast.Push(Module->FancyName + (Module->Enabled ? ": ON" : ": OFF"), 2.5f,
                            Module->Enabled ? ImVec4(0.2f, 1.f, 0.3f, 1.f) : ImVec4(1.f, 0.25f, 0.2f, 1.f));
                }
            }
            ImGui::EndMenu();
        }
        if (BeginMenu(ICON_FK_WRENCH " Developer"))
        {
            if (MenuItem(ICON_FK_BAN " Disable all modules", "", ForceModulesNoRender))
            {
                ForceModulesNoRender = !ForceModulesNoRender;
            }

            for (IModule* Module : Modules)
            {
                if (Module->IsDebug())
                {
                    bool WasEnabled = Module->Enabled;
                    Module->RenderMenuItem();
                    if (Module->Enabled != WasEnabled)
                        Toast.Push(Module->FancyName + (Module->Enabled ? ": ON" : ": OFF"), 2.5f,
                            Module->Enabled ? ImVec4(0.2f, 1.f, 0.3f, 1.f) : ImVec4(1.f, 0.25f, 0.2f, 1.f));
                }
            }

            LuaMgr->RenderModuleManagerMenuItem();

            if (MenuItem(ICON_FK_BUG " ImGui Demo", "", EnableImGuiDemo))
            {
                EnableImGuiDemo = !EnableImGuiDemo;
            }

            ImGui::EndMenu();
        }

        if (BeginMenu(ICON_FK_QUESTION_CIRCLE " Help"))
        {
            if (AboutMod and MenuItem(ICON_FK_INFO_CIRCLE " About", "", AboutMod->Enabled))
            {
                AboutMod->Enabled = !AboutMod->Enabled;
            }

            Separator();

            if (MenuItem(ICON_FK_GITHUB " Github"))
            {
                ShellExecuteA(NULL, "open", "https://github.com/TwinkieTweaks/Twinkie", NULL, NULL, SW_SHOWNORMAL);
            }

            if (MenuItem(ICON_FK_DISCORD_ALT " Discord"))
            {
                ShellExecuteA(NULL, "open", "https://discord.gg/kRZ4MdCkVf", NULL, NULL, SW_SHOWNORMAL);
            }

            ImGui::EndMenu();
        }

        LuaMgr->RunModulesRenderMainMenuItem();

        PopItemFlag();

        EndMainMenuBar();
    }

    if (EnableSettings) RenderSettings();

    if (Logger.EnableLog) Logger.RenderLog();

    if (EnableImGuiDemo) ShowDemoWindow(&EnableImGuiDemo);

    if (SelectedFont) PopFont();

    SelectedFont = (FontName == "BricolageGrotesque" or FontName == "") ? FontBricolageGrotesque : FontName == "DroidSans" ? FontDroidSans : FontName == "ComicNeue" ? FontComicNeue : FontMono;
}

void TwinkUi::RenderSettings()
{
    using namespace ImGui;

    if (Begin(ICON_FK_WRENCH " Settings", &EnableSettings, ImGuiWindowFlags_NoCollapse))
    {
        BeginChild("##TwinkieSettingsModulesList", { 150.f, 0.f }, ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);

        size_t CurModuleIdx = 0;

        bool UseBrandedSettingsTextColor = (ThemeIdx == 0);
        if (UseBrandedSettingsTextColor) PushStyleColor(ImGuiCol_Text, ColorConvertFloat4ToU32({ 1.f, 0.f, 1.f, 1.f }));
        if (Selectable("TwinkPlanet", (ActiveModuleIdx != CurModuleIdx) and (IsTwinkieSettingsOpen)))
        {
            IsTwinkieSettingsOpen = !IsTwinkieSettingsOpen;
        }
        if (UseBrandedSettingsTextColor) PopStyleColor();

        Separator();

        for (IModule* Module : Modules)
        {
            if (!Module->HasSettings())
            {
                CurModuleIdx++;
                continue;
            }

            if (Module == BlahajMod or Module == LSDMod or Module == CopperTrackerMod
                or Module == AudioVisMod or Module == ClockSessionTimerMod or Module == ImageMod
                or Module == PerformanceOverlayMod or Module == WheelIndicatorsMod or Module == RandomizerMod
                or Module == TwinkDiscordRPMod or Module == MediaControlsMod or Module == FireworksMod
                or Module == GamingModeMod or Module == ColorsMod)
            {
                bool IsFirstCustomModule = (Module == BlahajMod);
                if (IsFirstCustomModule) Separator();
            }

            Checkbox(("##" + Module->FancyName).c_str(), &Module->Enabled);
            SameLine();
            if (Selectable(Module->FancyName.c_str(), (ActiveModuleIdx == CurModuleIdx) and (!IsTwinkieSettingsOpen)))
            {
                ActiveModuleIdx = CurModuleIdx;
                IsTwinkieSettingsOpen = false;
            }

            CurModuleIdx++;
        }

        EndChild();

        SameLine();

        BeginGroup();

        PushStyleColor(ImGuiCol_ChildBg, ColorConvertFloat4ToU32({ 0.f, 0.f, 0.f, 0.f }));
        BeginChild("##TwinkieSettingsRender");
        PopStyleColor();

        if (!IsTwinkieSettingsOpen)
        {
            IModule* ActiveModule = Modules[ActiveModuleIdx];

            ActiveModule->RenderSettings();
        }
        else
        {
            SeparatorText("User Interface");
            SliderFloat("UI Scale", &UiScale, 0.25f, 5.f, "%.3f");
            if (Combo("Font", (int*)&FontIdx, g_FontNames, IM_ARRAYSIZE(g_FontNames)))
            {
                FontName = g_FontNames[FontIdx];
            }
            // Theme selector removed - Openplanet is the only theme now (ThemeIdx/ThemeName stay
            // at their default; SetupTwinkieImGuiStyle() is left in TwinkUi.cpp, just unreachable).

            // ── Positioning Lines ─────────────────────────────────────────
            SeparatorText("Positioning");
            Checkbox(ICON_FK_CROSSHAIRS " Positioning Lines", &EnablePositioningLines);
            if (IsItemHovered())
                SetTooltip("Shows a centre crosshair and rule-of-thirds grid to help position overlays.\n"
                           "Only visible while the menu is open (F3).\n"
                           "While dragging an overlay, its centre snaps to the nearest guide line.\n"
                           "Hold LCtrl  to bypass horizontal snaps (free vertical movement).\n"
                           "Hold LShift to bypass vertical snaps (free horizontal movement).");

            // ── Configs ───────────────────────────────────────────────────
            // Hidden from the Settings UI - code below is left in place, just unreachable.
            static const bool kShowConfigsSection = false;
            if (kShowConfigsSection) {
            SeparatorText("Configs");

            if (!m_ConfigActionStatus.empty())
                TextColored({ 0.4f, 1.f, 0.4f, 1.f }, "%s", m_ConfigActionStatus.c_str());

            BeginChild("##CfgList", ImVec2(0.f, 110.f), ImGuiChildFlags_Borders);
            if (m_ConfigFiles.empty())
                TextDisabled("No configs saved yet.");
            for (auto& cfg : m_ConfigFiles)
            {
                bool sel = (m_SelectedConfig == cfg);
                if (Selectable(cfg.c_str(), sel))
                    m_SelectedConfig = cfg;
            }
            EndChild();

            if (Button(ICON_FK_REFRESH " Refresh"))
                RefreshConfigList();
            SameLine();
            if (Button(ICON_FK_FOLDER_OPEN " Open Folder"))
                ShellExecuteA(nullptr, "open", GetConfigsDir().c_str(), nullptr, nullptr, SW_SHOWNORMAL);

            if (!m_SelectedConfig.empty())
            {
                if (Button(ICON_FK_PLAY " Load"))
                    LoadConfig(m_SelectedConfig);
                if (IsItemHovered()) SetTooltip("Apply this config to Twinkie");
                SameLine();
                if (Button(ICON_FK_FLOPPY_O " Save##overwrite"))
                    SaveConfig(m_SelectedConfig);
                if (IsItemHovered()) SetTooltip("Overwrite this config with current settings");
                SameLine();
                if (Button(ICON_FK_CLIPBOARD " Copy"))
                    ExportConfigToClipboard(m_SelectedConfig);
                if (IsItemHovered()) SetTooltip("Copy config text to clipboard for sharing");
                SameLine();
                PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.1f, 0.1f, 1.f));
                if (Button(ICON_FK_TRASH " Delete"))
                    DeleteConfig(m_SelectedConfig);
                PopStyleColor();
            }

            Separator();
            SetNextItemWidth(GetContentRegionAvail().x
                - CalcTextSize(ICON_FK_FLOPPY_O " Save").x
                - GetStyle().FramePadding.x * 2.f - GetStyle().ItemSpacing.x);
            InputText("##cfgname", m_ConfigNameBuf, sizeof(m_ConfigNameBuf));
            if (IsItemHovered()) SetTooltip("Config name");
            SameLine();
            if (Button(ICON_FK_FLOPPY_O " Save"))
            {
                SaveConfig(std::string(m_ConfigNameBuf));
                m_ConfigNameBuf[0] = '\0';
            }

            Separator();

            if (m_ConfigImportDone.load() && m_ConfigImportThread.joinable())
            {
                m_ConfigImportThread.join();
                m_ConfigImportDone.store(false);
                RefreshConfigList();
            }
            bool downloading = m_ConfigImportThread.joinable() && !m_ConfigImportDone.load();

            BeginDisabled(downloading);
            SetNextItemWidth(GetContentRegionAvail().x
                - CalcTextSize(ICON_FK_DOWNLOAD " Download").x
                - GetStyle().FramePadding.x * 2.f - GetStyle().ItemSpacing.x);
            InputText("##cfgurl", m_ConfigImportUrl, sizeof(m_ConfigImportUrl));
            if (IsItemHovered()) SetTooltip("URL of a .ini config file to download");
            SameLine();
            if (Button(ICON_FK_DOWNLOAD " Download"))
            {
                std::string urlStr(m_ConfigImportUrl);
                std::string dname = "downloaded";
                auto slash = urlStr.rfind('/');
                if (slash != std::string::npos) dname = urlStr.substr(slash + 1);
                auto dot = dname.rfind('.');
                if (dot != std::string::npos) dname = dname.substr(0, dot);
                if (dname.empty()) dname = "downloaded";
                ImportConfigFromURL(urlStr, dname);
            }
            EndDisabled();

            if (!m_ConfigImportStatus.empty())
                TextColored(downloading ? ImVec4{ 1.f, 0.8f, 0.2f, 1.f }
                                        : ImVec4{ 0.4f, 1.f, 0.4f, 1.f },
                    "%s", m_ConfigImportStatus.c_str());
            } // kShowConfigsSection

            SeparatorText("Autosave");
            SliderFloat("Autosave interval", &AutosaveIntervalMinutes, 0.1f, 60.f, "%.1f minutes");
            if (Button("Save settings"))
            {
                Logger.PrintInternal("Saving settings...");
                SettingsSave();
                Settings.Save();
                if (Settings.Status != 0)
                {
                    Logger.PrintErrorArgs("Could not save settings with reason {} ({})", Settings.Error, Settings.Status);
                }
                else
                {
                    Logger.PrintInternal("Settings saved successfully.");
                }
            }
        }

        EndChild();

        EndGroup();
    }
    End();
}

void TwinkUi::RenderAnyways()
{
    using namespace ImGui;

    if (!LuaModulesLoaded)
    {
        LuaMgr->GetModulesFromDocuments();
        LuaModulesLoaded = true;
        Logger.PrintInternalArgs("{} Lua module{} initialized.", LuaMgr->LuaModules.size(), LuaMgr->LuaModules.size() == 1 ? "" : "s");
    }

    if (SelectedFont) PushFont(SelectedFont, 14.f * UiScale);

    IoMgr->Update();

    for (IModule* Module : Modules)
    {
        if (ForceModulesNoRender) break;
        if (Module->Enabled) Module->RenderAnyways();
        Module->RenderInactive();
    }

    if (LuaMgr->ModuleManagerOpen) LuaMgr->RenderModuleManager();
	LuaMgr->RunModulesRenderInterface();

    Toast.Render();

    if (SelectedFont) PopFont();

    // Positioning guide lines + snap - only while the menu is visible
    if (EnablePositioningLines && DoRender)
    {
        // Snap first so the highlight reflects the post-snap centre position
        SnapOverlaysToGuideLines();

        auto* dl  = ImGui::GetForegroundDrawList();
        ImGuiContext* ctx = ImGui::GetCurrentContext();

        const float w = ImGui::GetIO().DisplaySize.x;
        const float h = ImGui::GetIO().DisplaySize.y;

        // Centre of the window currently being dragged (for snap highlights)
        ImGuiWindow* movRoot = (ctx && ctx->MovingWindow) ? ctx->MovingWindow->RootWindow : nullptr;
        const float movCx = movRoot ? movRoot->Pos.x + movRoot->Size.x * 0.5f : -1e9f;
        const float movCy = movRoot ? movRoot->Pos.y + movRoot->Size.y * 0.5f : -1e9f;
        const float kSnap = 12.f;

        struct Guide { float pos; bool center; };
        const Guide vG[] = { {w / 3.f, false}, {w * 0.5f, true}, {w * 2.f / 3.f, false} };
        const Guide hG[] = { {h / 3.f, false}, {h * 0.5f, true}, {h * 2.f / 3.f, false} };

        for (auto& g : vG)
        {
            bool  snap = movRoot && fabsf(movCx - g.pos) <= kSnap;
            ImU32 col  = snap ? IM_COL32(255, 220, 60, 255)
                       : g.center ? IM_COL32(255, 80, 80, 210)
                                  : IM_COL32(80, 150, 255, 170);
            float th   = snap ? 2.5f : g.center ? 1.5f : 1.f;
            dl->AddLine({ g.pos, 0.f }, { g.pos, h }, col, th);
        }
        for (auto& g : hG)
        {
            bool  snap = movRoot && fabsf(movCy - g.pos) <= kSnap;
            ImU32 col  = snap ? IM_COL32(255, 220, 60, 255)
                       : g.center ? IM_COL32(255, 80, 80, 210)
                                  : IM_COL32(80, 150, 255, 170);
            float th   = snap ? 2.5f : g.center ? 1.5f : 1.f;
            dl->AddLine({ 0.f, g.pos }, { w, g.pos }, col, th);
        }

        // Bottom-centre hint: show modifier key shortcuts; highlight the active one
        {
            const bool freeYActive = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);
            const bool freeXActive = ImGui::IsKeyDown(ImGuiKey_LeftShift);

            const char* partCtrl  = "LCtrl: free Y";
            const char* partSep   = "  |  ";
            const char* partShift = "LShift: free X";

            ImVec2 szCtrl  = ImGui::CalcTextSize(partCtrl);
            ImVec2 szSep   = ImGui::CalcTextSize(partSep);
            ImVec2 szShift = ImGui::CalcTextSize(partShift);

            float totalW = szCtrl.x + szSep.x + szShift.x;
            float hintX  = (w - totalW) * 0.5f;
            float hintY  = h - szCtrl.y - 10.f;

            ImU32 colCtrl  = freeYActive ? IM_COL32(255, 220, 60, 255) : IM_COL32(200, 200, 200, 180);
            ImU32 colSep   = IM_COL32(160, 160, 160, 140);
            ImU32 colShift = freeXActive ? IM_COL32(255, 220, 60, 255) : IM_COL32(200, 200, 200, 180);

            dl->AddText({ hintX,                       hintY }, colCtrl,  partCtrl);
            dl->AddText({ hintX + szCtrl.x,            hintY }, colSep,   partSep);
            dl->AddText({ hintX + szCtrl.x + szSep.x,  hintY }, colShift, partShift);
        }
    }

    bool TriggerAutosave = false;

    if (std::chrono::system_clock::now() - LastAutosaveTime > std::chrono::seconds((unsigned long long)round(AutosaveIntervalMinutes * 60.f)))
    {
        if (TrackmaniaMgr.IsPlaying())
        {
            if (TrackmaniaMgr.GetState() != TM::RaceState::Running and !(TrackmaniaMgr.IsOfficial() and TrackmaniaMgr.GetState() == TM::RaceState::BeforeStart))
            {
                TriggerAutosave = true;
            }
        }
        else
        {
            TriggerAutosave = true;
        }
    }

    if (TriggerAutosave)
    {
        Logger.PrintInternal("Autosaving settings...");
        SettingsSave();
        Settings.Save();

        if (Settings.Status != 0)
        {
            Logger.PrintErrorArgs("Could not save settings with reason {} ({})", Settings.Error, Settings.Status);
        }
        else
        {
            Logger.PrintInternal("Settings saved successfully.");
        }

        LastAutosaveTime = std::chrono::system_clock::now();
    }
}

// =============================================================================
// Positioning snap
// =============================================================================

void TwinkUi::SnapOverlaysToGuideLines()
{
    ImGuiContext* ctx = ImGui::GetCurrentContext();
    if (!ctx) return;
    ImGuiWindow* win = ctx->MovingWindow;
    if (!win) return;
    ImGuiWindow* root = win->RootWindow;
    if (!root) return;

    // Don't snap the settings window itself
    if (root->Name && strstr(root->Name, "Settings"))
        return;

    const float w = ImGui::GetIO().DisplaySize.x;
    const float h = ImGui::GetIO().DisplaySize.y;
    const float kSnap = 12.f;

    const float vLines[] = { w / 3.f, w * 0.5f, w * 2.f / 3.f };
    const float hLines[] = { h / 3.f, h * 0.5f, h * 2.f / 3.f };

    const float cx = root->Pos.x + root->Size.x * 0.5f;
    const float cy = root->Pos.y + root->Size.y * 0.5f;

    float newX = root->Pos.x, newY = root->Pos.y;
    bool  snapX = false,       snapY = false;

    for (float vl : vLines)
    {
        if (fabsf(cx - vl) <= kSnap)
        { newX = vl - root->Size.x * 0.5f; snapX = true; break; }
    }
    for (float hl : hLines)
    {
        if (fabsf(cy - hl) <= kSnap)
        { newY = hl - root->Size.y * 0.5f; snapY = true; break; }
    }

    // Modifier key overrides
    const bool freeY = ImGui::IsKeyDown(ImGuiKey_LeftCtrl);   // LCtrl  -> bypass horizontal-line snap
    const bool freeX = ImGui::IsKeyDown(ImGuiKey_LeftShift);  // LShift -> bypass vertical-line snap
    if (freeX) snapX = false;
    if (freeY) snapY = false;
    if (freeX) newX = root->Pos.x;
    if (freeY) newY = root->Pos.y;

    if (snapX || snapY)
    {
        ImGui::SetWindowPos(root, { newX, newY });
        // Adjust ImGui's drag anchor so it computes the snapped position next frame
        // (prevents the snap fighting the drag logic).
        const ImVec2 mp = ImGui::GetIO().MousePos;
        if (snapX) ctx->ActiveIdClickOffset.x = mp.x - newX;
        if (snapY) ctx->ActiveIdClickOffset.y = mp.y - newY;
    }
}

// =============================================================================
// Config system - named, saveable/loadable/shareable snapshots of settings.ini. Ported from
// TwinkieForever-master (the "Lolcin" fork)'s TwinkUi.cpp, adapted to this fork's own
// SettingMgr/GetDocumentsFolder helpers and "TwinkPlanet" naming.
// =============================================================================

std::string TwinkUi::GetConfigsDir() const
{
    return GetDocumentsFolder() + "\\TwinkPlanet\\Configs";
}

void TwinkUi::RefreshConfigList()
{
    m_ConfigFiles.clear();
    std::error_code ec;
    for (auto& entry : Filesystem::directory_iterator(Filesystem::u8path(GetConfigsDir()), ec))
    {
        if (entry.path().extension() == ".ini")
            m_ConfigFiles.push_back(entry.path().stem().string());
    }
    std::sort(m_ConfigFiles.begin(), m_ConfigFiles.end());
}

void TwinkUi::SaveConfig(const std::string& name)
{
    if (name.empty()) { m_ConfigActionStatus = "Enter a name first."; return; }

    SettingsSave();
    Settings.GenerateIni();

    // Build filtered snapshot: skip per-map grind stats and ghost data
    mINI::INIStructure cfg;
    for (auto& sec : Settings.IniStruct)
    {
        if (sec.first == "GhostEditor") continue;
        for (auto& kv : sec.second)
        {
            if (kv.first.size() >= 2 && kv.first.substr(0, 2) == "U_") continue;
            cfg[sec.first][kv.first] = kv.second;
        }
    }

    // Embed current display resolution so LoadConfig can rescale positions
    ImVec2 disp = ImGui::GetIO().DisplaySize;
    if (disp.x > 0.f && disp.y > 0.f)
    {
        cfg["Twinkie"]["SavedDisplayW"] = std::to_string((int)disp.x);
        cfg["Twinkie"]["SavedDisplayH"] = std::to_string((int)disp.y);
    }

    std::string dir = GetConfigsDir();
    Filesystem::create_directories(Filesystem::u8path(dir));
    mINI::INIFile f(dir + "\\" + name + ".ini");
    f.write(cfg, true);

    RefreshConfigList();
    m_ConfigActionStatus = "Saved \"" + name + "\"";
}

void TwinkUi::LoadConfig(const std::string& name)
{
    if (name.empty()) return;
    std::string path = GetConfigsDir() + "\\" + name + ".ini";

    mINI::INIFile f(path);
    mINI::INIStructure cfg;
    if (!f.read(cfg)) { m_ConfigActionStatus = "Could not read config."; return; }

    // Read saved display resolution for position scaling
    float savedW = 0.f, savedH = 0.f;
    try {
        if (cfg.has("Twinkie")) {
            auto& tw = cfg["Twinkie"];
            if (tw.has("saveddisplayw")) savedW = std::stof(tw["saveddisplayw"]);
            if (tw.has("saveddisplayh")) savedH = std::stof(tw["saveddisplayh"]);
            // mINI may lowercase keys; also try mixed-case
            if (savedW == 0.f && tw.has("SavedDisplayW")) savedW = std::stof(tw["SavedDisplayW"]);
            if (savedH == 0.f && tw.has("SavedDisplayH")) savedH = std::stof(tw["SavedDisplayH"]);
        }
    } catch (...) {}

    // Merge into main IniStruct (personal U_ stats are untouched since the config doesn't have them)
    for (auto& sec : cfg)
        for (auto& kv : sec.second)
            Settings.IniStruct[sec.first][kv.first] = kv.second;

    // Rescale overlay positions if the current display differs from the saved one
    ImVec2 curDisp = ImGui::GetIO().DisplaySize;
    if (savedW > 0.f && savedH > 0.f && curDisp.x > 0.f && curDisp.y > 0.f
        && (savedW != curDisp.x || savedH != curDisp.y))
    {
        float sx = curDisp.x / savedW;
        float sy = curDisp.y / savedH;

        // Case-insensitive suffix check
        auto endsWith = [](std::string s, const char* suf) {
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::string sf(suf); // suf already lowercase
            return s.size() >= sf.size() &&
                   s.compare(s.size() - sf.size(), sf.size(), sf) == 0;
        };

        // INIMap only exposes const_iterator, so collect changes then apply via set()
        for (auto& sec : Settings.IniStruct)
        {
            std::vector<std::pair<std::string, std::string>> changes;
            for (auto& kv : sec.second)
            {
                float val;
                try { val = std::stof(kv.second); } catch (...) { continue; }
                if      (endsWith(kv.first, "posx") || endsWith(kv.first, "wndposx"))
                    changes.emplace_back(kv.first, std::to_string(val * sx));
                else if (endsWith(kv.first, "posy") || endsWith(kv.first, "wndposy"))
                    changes.emplace_back(kv.first, std::to_string(val * sy));
            }
            for (auto& c : changes)
                Settings.IniStruct[sec.first].set(c.first, c.second);
        }

        m_ConfigActionStatus = "Loaded \"" + name + "\" (positions scaled for " +
                               std::to_string((int)curDisp.x) + "x" +
                               std::to_string((int)curDisp.y) + ")";
    }

    // Rebuild Tabs from updated IniStruct and re-apply to all modules
    Settings.Tabs.clear();
    Settings.LoadIni();
    SettingsInit();

    if (m_ConfigActionStatus.find("positions scaled") == std::string::npos)
        m_ConfigActionStatus = "Loaded \"" + name + "\"";
}

void TwinkUi::DeleteConfig(const std::string& name)
{
    if (name.empty()) return;
    Filesystem::remove(Filesystem::u8path(GetConfigsDir() + "\\" + name + ".ini"));
    if (m_SelectedConfig == name) m_SelectedConfig.clear();
    RefreshConfigList();
    m_ConfigActionStatus = "Deleted \"" + name + "\"";
}

void TwinkUi::ExportConfigToClipboard(const std::string& name)
{
    std::ifstream fs(GetConfigsDir() + "\\" + name + ".ini");
    if (!fs.is_open()) { m_ConfigActionStatus = "Cannot open config file."; return; }

    std::string content((std::istreambuf_iterator<char>(fs)),
                         std::istreambuf_iterator<char>());

    if (!OpenClipboard(nullptr)) { m_ConfigActionStatus = "Clipboard unavailable."; return; }
    EmptyClipboard();
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, content.size() + 1);
    if (hg)
    {
        void* p = GlobalLock(hg);
        memcpy(p, content.c_str(), content.size() + 1);
        GlobalUnlock(hg);
        SetClipboardData(CF_TEXT, hg);
    }
    CloseClipboard();
    m_ConfigActionStatus = "Copied \"" + name + "\" to clipboard!";
}

void TwinkUi::ImportConfigFromURL(const std::string& url, const std::string& destName)
{
    // Don't start a second download while one is running
    if (m_ConfigImportThread.joinable() && !m_ConfigImportDone.load()) return;
    if (m_ConfigImportThread.joinable()) m_ConfigImportThread.join();

    m_ConfigImportDone.store(false);
    m_ConfigImportStatus = "Downloading...";

    std::string dir = GetConfigsDir();
    m_ConfigImportThread = std::thread([this, url, destName, dir]()
    {
        // Parse URL
        int wlen = MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, nullptr, 0);
        std::wstring wurl(wlen, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, url.c_str(), -1, wurl.data(), wlen);
        if (!wurl.empty() && wurl.back() == L'\0') wurl.pop_back();

        wchar_t host[512] = {}, path[2048] = {};
        URL_COMPONENTS uc = {};
        uc.dwStructSize     = sizeof(uc);
        uc.lpszHostName     = host; uc.dwHostNameLength = (DWORD)std::size(host);
        uc.lpszUrlPath      = path; uc.dwUrlPathLength  = (DWORD)std::size(path);
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        { m_ConfigImportStatus = "Invalid URL."; m_ConfigImportDone.store(true); return; }

        HINTERNET hSess = WinHttpOpen(L"TwinkConfig/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSess) { m_ConfigImportStatus = "WinHTTP init failed."; m_ConfigImportDone.store(true); return; }

        HINTERNET hConn = WinHttpConnect(hSess, host, uc.nPort, 0);
        if (!hConn) { WinHttpCloseHandle(hSess); m_ConfigImportStatus = "Connect failed."; m_ConfigImportDone.store(true); return; }

        DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
        HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET",
            (path[0] ? path : L"/"), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
        if (!hReq)
        { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); m_ConfigImportStatus = "Request failed."; m_ConfigImportDone.store(true); return; }

        if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(hReq, nullptr))
        { WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
          m_ConfigImportStatus = "HTTP request failed."; m_ConfigImportDone.store(true); return; }

        std::vector<BYTE> data;
        DWORD avail = 0, read = 0;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
        {
            size_t off = data.size();
            data.resize(off + avail);
            WinHttpReadData(hReq, data.data() + off, avail, &read);
            data.resize(off + read);
        }
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);

        if (data.empty()) { m_ConfigImportStatus = "Empty response."; m_ConfigImportDone.store(true); return; }

        Filesystem::create_directories(Filesystem::u8path(dir));
        std::ofstream out(dir + "\\" + destName + ".ini", std::ios::binary);
        if (!out.is_open()) { m_ConfigImportStatus = "Cannot write file."; m_ConfigImportDone.store(true); return; }
        out.write(reinterpret_cast<const char*>(data.data()), (std::streamsize)data.size());
        out.close();

        m_ConfigImportStatus = "Downloaded \"" + destName + "\"!";
        m_ConfigImportDone.store(true);
    });
}