#include "CopperTracker.h"

// =============================================================================
// RenderInactive - tracking always runs regardless of Enabled
// =============================================================================
void CopperTrackerModule::RenderInactive()
{
    if (!Twinkie->IsProfileUnited()) return;

    int copper;
    if (m_CopperOffset >= 0)
    {
        uintptr_t profile = Twinkie->GetProfile();
        copper = profile ? Twinkie->Read<int>(profile + m_CopperOffset) : -1;
    }
    else
    {
        copper = Twinkie->GetCopper();
    }

    if (copper < 0)
    {
        m_LastCopper = -1;
        return;
    }

    if (m_SessionStart < 0)
    {
        m_SessionStart = copper;
        m_LastCopper   = copper;
        m_SessionDelta = 0;
    }

    if (m_LastCopper >= 0 && copper != m_LastCopper)
    {
        int delta = copper - m_LastCopper;
        m_SessionDelta += delta;
        m_Events.push_back({ delta });
        while ((int)m_Events.size() > kMaxEvents)
            m_Events.erase(m_Events.begin());
    }
    m_LastCopper = copper;
}

// =============================================================================
// RenderAnyways - overlay, only draws when Enabled
// =============================================================================
void CopperTrackerModule::RenderAnyways()
{
    if (!Enabled) return;
    if (!Twinkie->IsProfileUnited()) return;
    if (m_LastCopper < 0) return;

    using namespace ImGui;

    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration
               | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!*UiRenderEnabled) flags |= ImGuiWindowFlags_NoInputs;

    SetNextWindowBgAlpha(0.75f);
    if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
    else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }
    if (!Begin("##CopperTracker", nullptr, flags))
    {
        End();
        return;
    }
    m_WndPos = GetWindowPos();

    SetWindowFontScale(m_Scale);

    // Session delta display made inaccessible - code left in place, just permanently skipped.
    static const bool kSessionDeltaHidden = true;

    // Current copper + session delta on one line
    Text(ICON_FK_MONEY " %d", m_LastCopper);
    if (m_ShowSessionDelta && !kSessionDeltaHidden)
    {
        SameLine();
        if (m_SessionDelta > 0)
            TextColored({ 0.2f, 1.0f, 0.2f, 1.0f }, "(+%d)", m_SessionDelta);
        else if (m_SessionDelta < 0)
            TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "(%d)", m_SessionDelta);
        else
            TextDisabled("(+0)");
    }

    // Recent-events log (newest first)
    if (m_ShowEventLog && !m_Events.empty())
    {
        Separator();
        int shown = 0;
        for (int i = (int)m_Events.size() - 1; i >= 0 && shown < kOverlayVisible; i--, shown++)
        {
            const auto& e = m_Events[i];
            if (e.delta > 0)
                TextColored({ 0.2f, 1.0f, 0.2f, 1.0f }, "+%d", e.delta);
            else
                TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "%d", e.delta);
        }
    }

    SetWindowFontScale(1.f);

    End();
}

// =============================================================================
// RenderMenuItem
// =============================================================================
void CopperTrackerModule::RenderMenuItem()
{
    using namespace ImGui;

    BeginDisabled(!Twinkie->IsProfileUnited());

    if (MenuItem(ICON_FK_MONEY " Copper Tracker", "", Enabled))
        Enabled = !Enabled;

    if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && !Twinkie->IsProfileUnited())
        SetTooltip("Copper tracking requires a United/TMUF account.");

    EndDisabled();
}

// =============================================================================
// RenderSettings
// =============================================================================
void CopperTrackerModule::RenderSettings()
{
    using namespace ImGui;

    if (!Twinkie->IsProfileUnited())
    {
        TextDisabled("Copper tracking requires a United/TMUF account.");
        return;
    }

    int copper;
    if (m_CopperOffset >= 0)
    {
        uintptr_t profile = Twinkie->GetProfile();
        copper = profile ? Twinkie->Read<int>(profile + m_CopperOffset) : 0;
    }
    else
    {
        copper = Twinkie->GetCopper();
        if (copper < 0) copper = 0;
    }

    Text("Current coppers: %d", copper);
    if (m_SessionDelta > 0)
        TextColored({ 0.2f, 1.0f, 0.2f, 1.0f }, "Session total: +%d", m_SessionDelta);
    else if (m_SessionDelta < 0)
        TextColored({ 1.0f, 0.3f, 0.3f, 1.0f }, "Session total: %d", m_SessionDelta);
    else
        TextDisabled("Session total: +0");

    Separator();

    Checkbox("Show session delta (+0)", &m_ShowSessionDelta);
    if (IsItemHovered())
        SetTooltip("Show/hide the (+N) session change next to the copper count.");

    SliderFloat("Overlay scale", &m_Scale, 0.5f, 3.f, "%.2f");
    if (IsItemHovered())
        SetTooltip("Font scale for the overlay window.");

    Checkbox("Show event log in overlay", &m_ShowEventLog);
    if (IsItemHovered())
        SetTooltip("Display the last %d copper transactions in the overlay.", kOverlayVisible);

    if (Button(ICON_FK_REFRESH " Reset session"))
    {
        m_SessionStart = -1;
        m_LastCopper   = -1;
        m_SessionDelta = 0;
        m_Events.clear();
    }
    if (IsItemHovered())
        SetTooltip("Reset the session baseline and clear the event log.");

    Separator();

    // -- Raw profile scan - helps identify the correct copper offset --
    if (CollapsingHeader("Raw profile scan"))
    {
        uintptr_t profile = Twinkie->GetProfile();
        if (!profile)
        {
            TextDisabled("Profile pointer is null.");
        }
        else
        {
            Text("Profile base: 0x%08X", (unsigned)profile);
            SetNextItemWidth(80.f);
            InputInt("Copper offset (hex)", &m_DebugOffset, 4, 16);
            if (m_DebugOffset < 0) m_DebugOffset = 0;
            if (m_DebugOffset > 0x300) m_DebugOffset = 0x300;

            // Show a scrollable table of raw int values near the profile base
            if (BeginTable("##scan", 2, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY,
                           ImVec2(0, 180)))
            {
                TableSetupColumn("Offset"); TableSetupColumn("Value (int)");
                TableHeadersRow();
                for (int off = 0; off <= 0x100; off += 4)
                {
                    int val = Twinkie->Read<int>(profile + off);
                    bool highlighted = (off == m_DebugOffset);
                    if (highlighted) TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        ColorConvertFloat4ToU32({0.4f, 0.9f, 0.4f, 0.3f}));
                    TableNextRow();
                    TableSetColumnIndex(0); Text("+0x%02X", off);
                    TableSetColumnIndex(1); Text("%d", val);
                }
                EndTable();
            }

            TextDisabled("Find the row whose value matches your copper balance,");
            TextDisabled("note its offset, and set the spinner above to that value.");
            SameLine();
            if (Button("Use as copper offset"))
                m_CopperOffset = m_DebugOffset;

            if (m_CopperOffset >= 0)
            {
                int rawVal = Twinkie->Read<int>(profile + m_CopperOffset);
                Text("Reading copper from +0x%02X: %d", m_CopperOffset, rawVal);
            }
        }
    }
}

// =============================================================================
// Render - settings window
// =============================================================================
void CopperTrackerModule::Render()
{
}

// =============================================================================
// Settings
// =============================================================================
void CopperTrackerModule::SettingsInit(SettingMgr& Settings)
{
    Settings["CopperTracker"]["Enable"].GetAsBool(&Enabled);
    Settings["CopperTracker"]["ShowEventLog"].GetAsBool(&m_ShowEventLog);
    Settings["CopperTracker"]["ShowSessionDelta"].GetAsBool(&m_ShowSessionDelta);
    Settings["CopperTracker"]["Scale"].GetAsFloat(&m_Scale);
    if (m_Scale < 0.5f || m_Scale > 3.f) m_Scale = 1.0f;
    Settings["CopperTracker"]["CopperOffset"].GetAsInt(&m_CopperOffset);
    float px = 0.f, py = 0.f;
    Settings["CopperTracker"]["WndPosX"].GetAsFloat(&px);
    Settings["CopperTracker"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }
}

void CopperTrackerModule::SettingsSave(SettingMgr& Settings)
{
    Settings["CopperTracker"]["Enable"].Set(Enabled);
    Settings["CopperTracker"]["ShowEventLog"].Set(m_ShowEventLog);
    Settings["CopperTracker"]["ShowSessionDelta"].Set(m_ShowSessionDelta);
    Settings["CopperTracker"]["Scale"].Set(m_Scale);
    Settings["CopperTracker"]["CopperOffset"].Set(m_CopperOffset);
    Settings["CopperTracker"]["WndPosX"].Set(m_WndPos.x);
    Settings["CopperTracker"]["WndPosY"].Set(m_WndPos.y);
}
