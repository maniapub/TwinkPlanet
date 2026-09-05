#define NOMINMAX
#include "PerformanceOverlay.h"

// -- Color helpers --
ImVec4 PerformanceOverlayModule::FpsColor(float fps)
{
    if (fps >= 60.f) return ImVec4(0.2f, 1.f,  0.3f, 1.f);   // green
    if (fps >= 30.f) return ImVec4(1.f,  0.8f, 0.1f, 1.f);   // yellow
    return                   ImVec4(1.f,  0.25f,0.2f, 1.f);   // red
}

ImVec4 PerformanceOverlayModule::CpuColor(float pct)
{
    if (pct < 50.f) return ImVec4(0.2f, 1.f,  0.3f, 1.f);    // green
    if (pct < 80.f) return ImVec4(1.f,  0.8f, 0.1f, 1.f);    // yellow
    return                  ImVec4(1.f,  0.25f,0.2f, 1.f);    // red
}

// -- CPU usage (sampled every 0.5 s via GetSystemTimes) --
void PerformanceOverlayModule::UpdateCPU(float dt)
{
    m_CpuTimer += dt;
    if (m_CpuTimer < 0.5f) return;
    m_CpuTimer = 0.f;

    FILETIME idle, kernel, user;
    if (!GetSystemTimes(&idle, &kernel, &user)) return;

    ULARGE_INTEGER idleT, kernelT, userT;
    idleT.LowPart   = idle.dwLowDateTime;   idleT.HighPart   = idle.dwHighDateTime;
    kernelT.LowPart = kernel.dwLowDateTime; kernelT.HighPart = kernel.dwHighDateTime;
    userT.LowPart   = user.dwLowDateTime;   userT.HighPart   = user.dwHighDateTime;

    if (!m_CpuInitialized)
    {
        m_LastIdle = idleT; m_LastKernel = kernelT; m_LastUser = userT;
        m_CpuInitialized = true;
        return;
    }

    ULONGLONG idleDiff   = idleT.QuadPart   - m_LastIdle.QuadPart;
    ULONGLONG kernelDiff = kernelT.QuadPart - m_LastKernel.QuadPart;
    ULONGLONG userDiff   = userT.QuadPart   - m_LastUser.QuadPart;
    ULONGLONG total      = kernelDiff + userDiff;
    ULONGLONG busy       = total - idleDiff;

    m_CpuUsage = total > 0 ? (float)busy / total * 100.f : 0.f;

    m_LastIdle = idleT; m_LastKernel = kernelT; m_LastUser = userT;
}

// -- RenderAnyways --
// Always drawn regardless of menu state - only Enabled flag can hide it.
void PerformanceOverlayModule::RenderAnyways()
{
    if (!Enabled) return;
    using namespace ImGui;

    float dt  = GetIO().DeltaTime;
    float fps = GetIO().Framerate;
    float ms  = dt * 1000.f;

    if (m_ShowCPU) UpdateCPU(dt);

    // When m_SlowUpdate is on, only refresh the *displayed* numbers every
    // kDisplayRefreshInterval - the underlying measurements above still happen
    // every frame (or every 0.5s for CPU) either way, this just throttles how
    // often the on-screen text changes so it reads as a steady number instead
    // of flickering constantly. When off, the display just tracks live values.
    m_DisplayRefreshTimer += dt;
    if (!m_SlowUpdate || m_DisplayRefreshTimer >= kDisplayRefreshInterval)
    {
        m_DisplayRefreshTimer = 0.f;
        m_DisplayFps = fps;
        m_DisplayMs  = ms;
        m_DisplayCpu = m_CpuUsage;

        if (m_ShowRAM)
        {
            MEMORYSTATUSEX mem = {};
            mem.dwLength = sizeof(mem);
            GlobalMemoryStatusEx(&mem);
            m_DisplayRamUsedMB  = (DWORD)((mem.ullTotalPhys - mem.ullAvailPhys) / (1024 * 1024));
            m_DisplayRamTotalMB = (DWORD)(mem.ullTotalPhys / (1024 * 1024));
        }
    }

    // Window flags: always no-decoration; also lock when menu is hidden
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar
                           | ImGuiWindowFlags_AlwaysAutoResize
                           | ImGuiWindowFlags_NoCollapse
                           | ImGuiWindowFlags_NoFocusOnAppearing;

    if (*UiRenderEnabled)
    {
        // Menu open: show title bar so the window can be dragged
        SetNextWindowBgAlpha(0.65f);
        if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
        else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }
        if (!Begin("Performance###perf_ovl", nullptr, flags)) { End(); return; }
    }
    else
    {
        // Menu hidden: lock in place, no title bar
        flags |= ImGuiWindowFlags_NoTitleBar
               | ImGuiWindowFlags_NoInputs
               | ImGuiWindowFlags_NoMove;
        SetNextWindowBgAlpha(0.65f);
        if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
        else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }
        if (!Begin("###perf_ovl", nullptr, flags)) { End(); return; }
    }
    m_WndPos = GetWindowPos();

    const char* msFmt  = m_NoDecimals ? "%.0f ms" : "%.2f ms";
    const char* cpuFmt = m_NoDecimals ? "%.0f%%"  : "%.1f%%";

    // -- Metrics (all read from the throttled m_Display* cache, not the live values) --
    if (m_ShowFPS)
    {
        TextColored(FpsColor(m_DisplayFps), "FPS");
        SameLine(60);
        TextColored(FpsColor(m_DisplayFps), "%.0f", m_DisplayFps);
    }

    if (m_ShowFrameTime)
    {
        TextColored(FpsColor(m_DisplayFps), "Frame");
        SameLine(60);
        TextColored(FpsColor(m_DisplayFps), msFmt, m_DisplayMs);
    }

    if (m_ShowCPU)
    {
        TextColored(CpuColor(m_DisplayCpu), "CPU");
        SameLine(60);
        TextColored(CpuColor(m_DisplayCpu), cpuFmt, m_DisplayCpu);
    }

    if (m_ShowRAM)
    {
        float ramPct = m_DisplayRamTotalMB > 0 ? (float)m_DisplayRamUsedMB / m_DisplayRamTotalMB * 100.f : 0.f;
        TextColored(CpuColor(ramPct), "RAM");
        SameLine(60);
        TextColored(CpuColor(ramPct), "%u / %u MB", m_DisplayRamUsedMB, m_DisplayRamTotalMB);
    }

    End();
}

// -- RenderSettings --
void PerformanceOverlayModule::RenderSettings()
{
    using namespace ImGui;
    TextDisabled("Overlay stays visible when menu is hidden.");
    Spacing();
    Checkbox("FPS",        &m_ShowFPS);
    Checkbox("Frame time", &m_ShowFrameTime);
    Checkbox("CPU usage",  &m_ShowCPU);
    Checkbox("RAM usage",  &m_ShowRAM);
    Separator();
    Checkbox("No decimals", &m_NoDecimals);
    if (IsItemHovered())
        SetTooltip("Round frame time and CPU usage to whole numbers.");

    Checkbox("Slow update", &m_SlowUpdate);
    if (IsItemHovered())
        SetTooltip("Smooths the numbers by only refreshing them a few times a second\ninstead of every frame. Turn off for live, instantly-updating values.");
}

// -- Render (settings) --
void PerformanceOverlayModule::Render()
{
}

// -- RenderMenuItem --
void PerformanceOverlayModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_BAR_CHART " Performance Overlay", "", Enabled))
        Enabled = !Enabled;
}

// -- Settings --
void PerformanceOverlayModule::SettingsInit(SettingMgr& Settings)
{
    Settings["PerformanceOverlay"]["Enable"].GetAsBool(&Enabled);
    Settings["PerformanceOverlay"]["ShowFPS"].GetAsBool(&m_ShowFPS);
    Settings["PerformanceOverlay"]["ShowFrameTime"].GetAsBool(&m_ShowFrameTime);
    Settings["PerformanceOverlay"]["ShowCPU"].GetAsBool(&m_ShowCPU);
    Settings["PerformanceOverlay"]["ShowRAM"].GetAsBool(&m_ShowRAM);
    Settings["PerformanceOverlay"]["NoDecimals"].GetAsBool(&m_NoDecimals);
    Settings["PerformanceOverlay"]["SlowUpdate"].GetAsBool(&m_SlowUpdate);
    float px = 0.f, py = 0.f;
    Settings["PerformanceOverlay"]["WndPosX"].GetAsFloat(&px);
    Settings["PerformanceOverlay"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }
}

void PerformanceOverlayModule::SettingsSave(SettingMgr& Settings)
{
    Settings["PerformanceOverlay"]["Enable"].Set(Enabled);
    Settings["PerformanceOverlay"]["ShowFPS"].Set(m_ShowFPS);
    Settings["PerformanceOverlay"]["ShowFrameTime"].Set(m_ShowFrameTime);
    Settings["PerformanceOverlay"]["ShowCPU"].Set(m_ShowCPU);
    Settings["PerformanceOverlay"]["ShowRAM"].Set(m_ShowRAM);
    Settings["PerformanceOverlay"]["NoDecimals"].Set(m_NoDecimals);
    Settings["PerformanceOverlay"]["SlowUpdate"].Set(m_SlowUpdate);
    Settings["PerformanceOverlay"]["WndPosX"].Set(m_WndPos.x);
    Settings["PerformanceOverlay"]["WndPosY"].Set(m_WndPos.y);
}
