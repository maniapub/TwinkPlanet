#include "GamingMode.h"
#include <cstring>

void GamingModeModule::RenderAnyways()
{
    if (!Enabled) return;

    float t = (float)ImGui::GetTime() * m_CycleSpeed;
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(fmodf(t, 1.0f), 1.0f, 1.0f, r, g, b);
    ImU32 col = IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), 255);

    std::string text = DisplayText();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    dl->AddText(ImGui::GetFont(), m_FontSize, ImVec2(m_PosX, m_PosY), col, text.c_str());

    // Drag to reposition and scroll to scale (only when menu is open)
    if (!*UiRenderEnabled) return;

    ImVec2 defaultSize = ImGui::CalcTextSize(text.c_str());
    float  scale       = m_FontSize / ImGui::GetFontSize();
    ImVec2 tmin        = { m_PosX, m_PosY };
    ImVec2 tmax        = { m_PosX + defaultSize.x * scale, m_PosY + defaultSize.y * scale };

    bool hovered = ImGui::IsMouseHoveringRect(tmin, tmax, false);

    if (hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 1.f))
    {
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        m_PosX += delta.x;
        m_PosY += delta.y;
    }

    float wheel = ImGui::GetIO().MouseWheel;
    if (hovered && wheel != 0.f)
    {
        m_FontSize += wheel * 5.f;
        if (m_FontSize < 10.f)  m_FontSize = 10.f;
        if (m_FontSize > 300.f) m_FontSize = 300.f;
    }

    // Subtle highlight when hoverable
    if (hovered)
        dl->AddRect(tmin, tmax, IM_COL32(255, 255, 255, 60), 0.f, 0, 1.f);
}

void GamingModeModule::RenderSettings()
{
    using namespace ImGui;
    if (InputText("Text##gm", m_CustomTextBuf, sizeof(m_CustomTextBuf)))
        m_CustomText = m_CustomTextBuf;
    if (IsItemHovered())
        SetTooltip("Leave empty to default to \"GAMING\".");
    SliderFloat("Font size##gm",    &m_FontSize,   20.f, 200.f, "%.0f px");
    SliderFloat("Cycle speed##gm",  &m_CycleSpeed, 0.1f, 5.f,   "%.2f");
    SliderFloat("X##gm", &m_PosX, 0.f, GetIO().DisplaySize.x > 0 ? GetIO().DisplaySize.x : 1920.f, "%.0f");
    SliderFloat("Y##gm", &m_PosY, 0.f, GetIO().DisplaySize.y > 0 ? GetIO().DisplaySize.y : 1080.f, "%.0f");
}

void GamingModeModule::Render()
{
}

void GamingModeModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_GAMEPAD " GAMING Mode", "", Enabled))
        Enabled = !Enabled;
}

void GamingModeModule::SettingsInit(SettingMgr& Settings)
{
    Settings["GamingMode"]["Enable"].GetAsBool(&Enabled);
    Settings["GamingMode"]["FontSize"].GetAsFloat(&m_FontSize);
    Settings["GamingMode"]["CycleSpeed"].GetAsFloat(&m_CycleSpeed);
    Settings["GamingMode"]["PosX"].GetAsFloat(&m_PosX);
    Settings["GamingMode"]["PosY"].GetAsFloat(&m_PosY);
    Settings["GamingMode"]["Text"].GetAsString(&m_CustomText);
    strncpy_s(m_CustomTextBuf, m_CustomText.c_str(), sizeof(m_CustomTextBuf) - 1);
}

void GamingModeModule::SettingsSave(SettingMgr& Settings)
{
    Settings["GamingMode"]["Enable"].Set(Enabled);
    Settings["GamingMode"]["FontSize"].Set(m_FontSize);
    Settings["GamingMode"]["CycleSpeed"].Set(m_CycleSpeed);
    Settings["GamingMode"]["PosX"].Set(m_PosX);
    Settings["GamingMode"]["PosY"].Set(m_PosY);
    Settings["GamingMode"]["Text"].Set(m_CustomText);
}
