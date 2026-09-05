#define NOMINMAX
#include "ClockSessionTimer.h"

// =============================================================================
// Clock window
// =============================================================================
void ClockSessionTimerModule::RenderClockWindow()
{
    time_t rawTime = time(nullptr);
    struct tm ti;
    localtime_s(&ti, &rawTime);

    char timeBuf[32], dateBuf[32];
    if (m_Show24h)
        strftime(timeBuf, sizeof(timeBuf), m_ShowSecs ? "%H:%M:%S" : "%H:%M", &ti);
    else
        strftime(timeBuf, sizeof(timeBuf), m_ShowSecs ? "%I:%M:%S %p" : "%I:%M %p", &ti);
    strftime(dateBuf, sizeof(dateBuf), "%d/%m/%Y", &ti);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse  |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (!*UiRenderEnabled)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowBgAlpha(m_ClockBgAlpha);
    if (m_ClockPosSet) { ImGui::SetNextWindowPos(m_ClockPos, ImGuiCond_Always); m_ClockPosSet = false; }
    else                { ImGui::SetNextWindowPos(m_ClockPos, ImGuiCond_Appearing); }

    if (!ImGui::Begin("##Clock", nullptr, flags)) { ImGui::End(); return; }
    m_ClockPos = ImGui::GetWindowPos();

    ImGui::SetWindowFontScale(m_ClockFontScale);
    ImGui::PushStyleColor(ImGuiCol_Text, m_ClockColor);
    ImGui::TextUnformatted(timeBuf);
    if (m_ShowDate)
        ImGui::TextDisabled("%s", dateBuf);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.f);

    ImGui::End();
}

// =============================================================================
// Session timer window
// =============================================================================
void ClockSessionTimerModule::RenderSessionWindow()
{
    auto secs = (unsigned long long)std::chrono::duration_cast<std::chrono::seconds>(
                    SteadyClock::now() - m_SessionStart).count();
    unsigned int h = (unsigned int)(secs / 3600);
    unsigned int m = (unsigned int)((secs % 3600) / 60);
    unsigned int s = (unsigned int)(secs % 60);

    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%02u:%02u:%02u", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%02u:%02u", m, s);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar         |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse  |
        ImGuiWindowFlags_AlwaysAutoResize   |
        ImGuiWindowFlags_NoCollapse         |
        ImGuiWindowFlags_NoBringToFrontOnFocus |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav;

    if (!*UiRenderEnabled)
        flags |= ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoInputs;

    ImGui::SetNextWindowBgAlpha(m_SessionBgAlpha);
    if (m_SessionPosSet) { ImGui::SetNextWindowPos(m_SessionPos, ImGuiCond_Always); m_SessionPosSet = false; }
    else                  { ImGui::SetNextWindowPos(m_SessionPos, ImGuiCond_Appearing); }

    if (!ImGui::Begin("##SessionTimer", nullptr, flags)) { ImGui::End(); return; }
    m_SessionPos = ImGui::GetWindowPos();

    ImGui::SetWindowFontScale(m_SessionFontScale);
    ImGui::PushStyleColor(ImGuiCol_Text, m_SessionColor);
    ImGui::TextUnformatted(ICON_FK_CLOCK_O " Session");
    ImGui::TextUnformatted(buf);
    ImGui::PopStyleColor();
    ImGui::SetWindowFontScale(1.f);

    ImGui::End();
}

// =============================================================================
// IModule overrides
// =============================================================================
void ClockSessionTimerModule::RenderAnyways()
{
    if (!Enabled) return;
    if (m_ShowClock)   RenderClockWindow();
    if (m_ShowSession) RenderSessionWindow();
}

void ClockSessionTimerModule::RenderMenuItem()
{
    if (ImGui::MenuItem(ICON_FK_CLOCK_O " Clock & Session Timer", "", Enabled))
        Enabled = !Enabled;
}

void ClockSessionTimerModule::RenderSettings()
{
    using namespace ImGui;

    SeparatorText("Clock");
    Checkbox("Show clock", &m_ShowClock);
    if (m_ShowClock)
    {
        Checkbox("24-hour format", &m_Show24h);
        Checkbox("Show seconds",   &m_ShowSecs);
        Checkbox("Show date",      &m_ShowDate);
        SliderFloat("Font scale##clock", &m_ClockFontScale, 0.5f, 4.f, "%.2f");
        SliderFloat("Background##clock", &m_ClockBgAlpha,   0.f,  1.f, "%.2f");
        ColorEdit4("Text color##clock",  (float*)&m_ClockColor, ImGuiColorEditFlags_NoInputs);
    }

    Separator();
    SeparatorText("Session Timer");
    Checkbox("Show session timer", &m_ShowSession);
    if (m_ShowSession)
    {
        if (Button(ICON_FK_REFRESH " Reset timer"))
            m_SessionStart = SteadyClock::now();
        SliderFloat("Font scale##session", &m_SessionFontScale, 0.5f, 4.f, "%.2f");
        SliderFloat("Background##session", &m_SessionBgAlpha,   0.f,  1.f, "%.2f");
        ColorEdit4("Text color##session",  (float*)&m_SessionColor, ImGuiColorEditFlags_NoInputs);
    }
}

void ClockSessionTimerModule::SettingsInit(SettingMgr& Settings)
{
    Settings["ClockSessionTimer"]["Enable"].GetAsBool(&Enabled);

    Settings["ClockSessionTimer"]["ShowClock"].GetAsBool(&m_ShowClock);
    Settings["ClockSessionTimer"]["Show24h"].GetAsBool(&m_Show24h);
    Settings["ClockSessionTimer"]["ShowSecs"].GetAsBool(&m_ShowSecs);
    Settings["ClockSessionTimer"]["ShowDate"].GetAsBool(&m_ShowDate);
    Settings["ClockSessionTimer"]["ClockFontScale"].GetAsFloat(&m_ClockFontScale);
    Settings["ClockSessionTimer"]["ClockBgAlpha"].GetAsFloat(&m_ClockBgAlpha);
    Settings["ClockSessionTimer"]["ClockColorR"].GetAsFloat(&m_ClockColor.x);
    Settings["ClockSessionTimer"]["ClockColorG"].GetAsFloat(&m_ClockColor.y);
    Settings["ClockSessionTimer"]["ClockColorB"].GetAsFloat(&m_ClockColor.z);
    Settings["ClockSessionTimer"]["ClockColorA"].GetAsFloat(&m_ClockColor.w);
    float cpx = 0.f, cpy = 0.f;
    Settings["ClockSessionTimer"]["ClockPosX"].GetAsFloat(&cpx);
    Settings["ClockSessionTimer"]["ClockPosY"].GetAsFloat(&cpy);
    if (cpx != 0.f || cpy != 0.f) { m_ClockPos = { cpx, cpy }; m_ClockPosSet = true; }

    Settings["ClockSessionTimer"]["ShowSession"].GetAsBool(&m_ShowSession);
    Settings["ClockSessionTimer"]["SessionFontScale"].GetAsFloat(&m_SessionFontScale);
    Settings["ClockSessionTimer"]["SessionBgAlpha"].GetAsFloat(&m_SessionBgAlpha);
    Settings["ClockSessionTimer"]["SessionColorR"].GetAsFloat(&m_SessionColor.x);
    Settings["ClockSessionTimer"]["SessionColorG"].GetAsFloat(&m_SessionColor.y);
    Settings["ClockSessionTimer"]["SessionColorB"].GetAsFloat(&m_SessionColor.z);
    Settings["ClockSessionTimer"]["SessionColorA"].GetAsFloat(&m_SessionColor.w);
    float spx = 0.f, spy = 0.f;
    Settings["ClockSessionTimer"]["SessionPosX"].GetAsFloat(&spx);
    Settings["ClockSessionTimer"]["SessionPosY"].GetAsFloat(&spy);
    if (spx != 0.f || spy != 0.f) { m_SessionPos = { spx, spy }; m_SessionPosSet = true; }
}

void ClockSessionTimerModule::SettingsSave(SettingMgr& Settings)
{
    Settings["ClockSessionTimer"]["Enable"].Set(Enabled);

    Settings["ClockSessionTimer"]["ShowClock"].Set(m_ShowClock);
    Settings["ClockSessionTimer"]["Show24h"].Set(m_Show24h);
    Settings["ClockSessionTimer"]["ShowSecs"].Set(m_ShowSecs);
    Settings["ClockSessionTimer"]["ShowDate"].Set(m_ShowDate);
    Settings["ClockSessionTimer"]["ClockFontScale"].Set(m_ClockFontScale);
    Settings["ClockSessionTimer"]["ClockBgAlpha"].Set(m_ClockBgAlpha);
    Settings["ClockSessionTimer"]["ClockColorR"].Set(m_ClockColor.x);
    Settings["ClockSessionTimer"]["ClockColorG"].Set(m_ClockColor.y);
    Settings["ClockSessionTimer"]["ClockColorB"].Set(m_ClockColor.z);
    Settings["ClockSessionTimer"]["ClockColorA"].Set(m_ClockColor.w);
    Settings["ClockSessionTimer"]["ClockPosX"].Set(m_ClockPos.x);
    Settings["ClockSessionTimer"]["ClockPosY"].Set(m_ClockPos.y);

    Settings["ClockSessionTimer"]["ShowSession"].Set(m_ShowSession);
    Settings["ClockSessionTimer"]["SessionFontScale"].Set(m_SessionFontScale);
    Settings["ClockSessionTimer"]["SessionBgAlpha"].Set(m_SessionBgAlpha);
    Settings["ClockSessionTimer"]["SessionColorR"].Set(m_SessionColor.x);
    Settings["ClockSessionTimer"]["SessionColorG"].Set(m_SessionColor.y);
    Settings["ClockSessionTimer"]["SessionColorB"].Set(m_SessionColor.z);
    Settings["ClockSessionTimer"]["SessionColorA"].Set(m_SessionColor.w);
    Settings["ClockSessionTimer"]["SessionPosX"].Set(m_SessionPos.x);
    Settings["ClockSessionTimer"]["SessionPosY"].Set(m_SessionPos.y);
}
