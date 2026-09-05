#include "WheelIndicators.h"
#include <algorithm>

// Wheel index order in TM Forever: 0=FL, 1=FR, 2=RL, 3=RR
static const char* kWheelLabels[4] = { "FL", "FR", "RL", "RR" };

// Grid layout: [row][col] -> wheel index
//   FL(0)  FR(1)
//   RL(2)  RR(3)
static const int kLayout[2][2] = { {0, 1}, {2, 3} };

void WheelIndicatorsModule::RenderAnyways()
{
    if (!Enabled) return;
    if (!Twinkie->IsPlaying()) return;

    using namespace ImGui;

    auto flags = ImGuiWindowFlags_NoTitleBar    | ImGuiWindowFlags_NoDecoration
               | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing
               | ImGuiWindowFlags_NoScrollbar;
    if (!*UiRenderEnabled)
        flags |= ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoMove;

    SetNextWindowBgAlpha(m_Opacity);
    if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
    else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }
    if (!Begin("##WheelIndicators", nullptr, flags)) { End(); return; }
    m_WndPos = GetWindowPos();

    // -- Read wheel state --
    auto wheels = Twinkie->GetVehicleWheels();
    bool contact[4] = {}, slip[4] = {};
    bool valid = (wheels.Size >= 4 && wheels.Ptr != 0);
    if (valid) {
        for (int i = 0; i < 4; i++) {
            contact[i] = Twinkie->GetVehicleWheelIsContactingGround(wheels[i]);
            slip[i]    = Twinkie->GetVehicleWheelIsSlipping(wheels[i]);
        }
    }

    // -- Smooth transitions --
    for (int i = 0; i < 4; i++) {
        float tc = contact[i] ? 1.f : 0.f;
        float ts = (contact[i] && slip[i]) ? 1.f : 0.f;
        m_ContactSmooth[i] += (tc - m_ContactSmooth[i]) * (tc > m_ContactSmooth[i] ? 0.40f : 0.10f);
        m_SlipSmooth[i]    += (ts - m_SlipSmooth[i])    * (ts > m_SlipSmooth[i]    ? 0.40f : 0.10f);
    }

    // -- Layout --
    ImDrawList* dl   = GetWindowDrawList();
    ImVec2      orig = GetCursorScreenPos();

    const float tileW  = m_WheelSize * 1.9f;
    const float tileH  = m_WheelSize;
    const float gap    = 5.f;
    const float pad    = 7.f;
    const float corner = 4.f;

    // -- Subtle axle lines (drawn first, under tiles) --
    // Horizontal between rows
    float midY  = orig.y + pad + tileH + gap * 0.5f;
    float midX  = orig.x + pad + tileW + gap * 0.5f;
    float right = orig.x + pad + tileW * 2.f + gap;
    float bot   = orig.y + pad + tileH * 2.f + gap;
    dl->AddLine(ImVec2(orig.x + pad, midY), ImVec2(right, midY), IM_COL32(90, 90, 100, 90), 1.f);
    dl->AddLine(ImVec2(midX, orig.y + pad), ImVec2(midX, bot),   IM_COL32(90, 90, 100, 90), 1.f);

    // -- Three anchor colors: air / ground / slip --
    // Interpolated as:  fill = lerp(air, lerp(ground, slip, slipT), contactT)
    struct C3 { float r, g, b; };
    auto lp = [](C3 a, C3 b, float t) -> C3 {
        return { a.r+(b.r-a.r)*t, a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t };
    };
    auto u32 = [](C3 c, int a) -> ImU32 {
        return IM_COL32((int)(c.r*255),(int)(c.g*255),(int)(c.b*255),a);
    };

    constexpr C3 kAir    = { 0.09f, 0.09f, 0.11f };
    constexpr C3 kGround = { 0.13f, 0.71f, 0.33f };
    constexpr C3 kSlip   = { 0.93f, 0.41f, 0.05f };

    // -- Tiles --
    for (int row = 0; row < 2; row++) {
        for (int col = 0; col < 2; col++) {
            int wi = kLayout[row][col];
            float x0 = orig.x + pad + col * (tileW + gap);
            float y0 = orig.y + pad + row * (tileH + gap);
            float x1 = x0 + tileW;
            float y1 = y0 + tileH;

            float ct = m_ContactSmooth[wi];
            float st = m_SlipSmooth[wi];
            C3 fill  = lp(kAir, lp(kGround, kSlip, st), ct);

            // Fill: dim when airborne, bright when active
            dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1),
                              u32(fill, (int)((0.30f + ct * 0.70f) * 255)), corner);
            // Border: same hue, slightly brighter
            C3 border = lp(fill, { 1.f, 1.f, 1.f }, 0.15f);
            dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1),
                        u32(border, (int)((0.45f + ct * 0.55f) * 255)), corner, 0, 1.2f);

            // Label - centered, ghosted white
            if (m_ShowLabels) {
                ImVec2 ts = CalcTextSize(kWheelLabels[wi]);
                dl->AddText(ImVec2(x0 + (tileW - ts.x) * 0.5f,
                                   y0 + (tileH - ts.y) * 0.5f),
                            IM_COL32(255, 255, 255, 80), kWheelLabels[wi]);
            }
        }
    }

    Dummy(ImVec2(pad * 2.f + tileW * 2.f + gap,
                 pad * 2.f + tileH * 2.f + gap));
    End();
}

void WheelIndicatorsModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_CIRCLE_O " Wheel Indicators", "", Enabled))
        Enabled = !Enabled;
}

void WheelIndicatorsModule::RenderSettings()
{
    using namespace ImGui;
    SliderFloat("Tile size",   &m_WheelSize, 10.f, 36.f, "%.0f px");
    SliderFloat("Opacity",     &m_Opacity,   0.1f, 1.0f, "%.2f");
    Checkbox("Show labels",              &m_ShowLabels);
}

void WheelIndicatorsModule::Render()
{
}

void WheelIndicatorsModule::SettingsInit(SettingMgr& Settings)
{
    Settings["WheelIndicators"]["Enable"].GetAsBool  (&Enabled);
    Settings["WheelIndicators"]["WheelSize"].GetAsFloat(&m_WheelSize);
    Settings["WheelIndicators"]["Opacity"].GetAsFloat  (&m_Opacity);
    Settings["WheelIndicators"]["ShowLabels"].GetAsBool(&m_ShowLabels);
    float px = 0.f, py = 0.f;
    Settings["WheelIndicators"]["WndPosX"].GetAsFloat(&px);
    Settings["WheelIndicators"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }
}

void WheelIndicatorsModule::SettingsSave(SettingMgr& Settings)
{
    Settings["WheelIndicators"]["Enable"].Set(Enabled);
    Settings["WheelIndicators"]["WheelSize"].Set(m_WheelSize);
    Settings["WheelIndicators"]["Opacity"].Set(m_Opacity);
    Settings["WheelIndicators"]["ShowLabels"].Set(m_ShowLabels);
    Settings["WheelIndicators"]["WndPosX"].Set(m_WndPos.x);
    Settings["WheelIndicators"]["WndPosY"].Set(m_WndPos.y);
}
