#include "LSD.h"
#include "../../imgui-dx9/imgui_internal.h"

static float WrapHue(float h)
{
    h = fmodf(h, 1.0f);
    if (h < 0.0f) h += 1.0f;
    return h;
}

static ImU32 HSV(float h, float s, float v, float a)
{
    float r, g, b;
    ImGui::ColorConvertHSVtoRGB(WrapHue(h), s, v, r, g, b);
    return IM_COL32(
        static_cast<int>(r * 255),
        static_cast<int>(g * 255),
        static_cast<int>(b * 255),
        static_cast<int>(a * 255));
}

// =============================================================================
// RenderAnyways - diagonal rainbow overlay, seamless
// =============================================================================
void LSDModule::RenderAnyways()
{
    if (!Enabled) return;
    if (m_OnlyWhilePlaying && !Twinkie->IsPlaying()) return;

    m_Hue = WrapHue(m_Hue + m_Speed * ImGui::GetIO().DeltaTime);

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const float W  = ImGui::GetIO().DisplaySize.x;
    const float H  = ImGui::GetIO().DisplaySize.y;

    // Each strip is a parallelogram along the 45-degree diagonal (x + y = const).
    // The full diagonal runs from 0 to W+H. We extend the strips above and
    // below the screen by 'ext' so the GPU's clip rect handles the edges -
    // no manual clipping needed, and adjacent strips share exact coordinates
    // so there are zero seams.

    const int   N    = 48;
    const float span = W + H;
    const float ext  = span;   // extend far enough to cover all screen corners

    const ImVec2 uv  = dl->_Data->TexUvWhitePixel;
    dl->PrimReserve(N * 6, N * 4);

    for (int i = 0; i < N; i++)
    {
        const float t0 = static_cast<float>(i)     / N;
        const float t1 = static_cast<float>(i + 1) / N;
        const float d0 = t0 * span;
        const float d1 = t1 * span;

        const ImU32 c0 = HSV(m_Hue + t0, 1.0f, 1.0f, m_Alpha);
        const ImU32 c1 = HSV(m_Hue + t1, 1.0f, 1.0f, m_Alpha);

        // For the 45-degree diagonal x + y = d, at any y: x = d - y.
        // The left edge (d0) and right edge (d1) are extended from y=-ext to y=H+ext.
        const ImVec2 tl = { d0 + ext,        -ext     }; // top of left edge
        const ImVec2 bl = { d0 - (H + ext),   H + ext }; // bottom of left edge
        const ImVec2 br = { d1 - (H + ext),   H + ext }; // bottom of right edge
        const ImVec2 tr = { d1 + ext,        -ext     }; // top of right edge

        const ImDrawIdx base = static_cast<ImDrawIdx>(dl->_VtxCurrentIdx);
        dl->PrimWriteVtx(tl, uv, c0);
        dl->PrimWriteVtx(bl, uv, c0);
        dl->PrimWriteVtx(br, uv, c1);
        dl->PrimWriteVtx(tr, uv, c1);
        // Two triangles: TL-BL-BR and TL-BR-TR
        dl->PrimWriteIdx(base + 0);
        dl->PrimWriteIdx(base + 1);
        dl->PrimWriteIdx(base + 2);
        dl->PrimWriteIdx(base + 0);
        dl->PrimWriteIdx(base + 2);
        dl->PrimWriteIdx(base + 3);
    }
}

// =============================================================================
// RenderMenuItem
// =============================================================================
void LSDModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_ADJUST " Acid", "", Enabled))
        Enabled = !Enabled;
}

// =============================================================================
// RenderSettings
// =============================================================================
void LSDModule::RenderSettings()
{
    using namespace ImGui;
    SetNextItemWidth(220);
    SliderFloat("Speed##lsd", &m_Speed, 0.05f, 5.0f, "%.2f cycles/s");
    if (IsItemHovered())
        SetTooltip("How fast the colors cycle.\n0.05 = very slow, 5.0 = very fast.");

    SetNextItemWidth(220);
    SliderFloat("Intensity##lsd", &m_Alpha, 0.01f, 1.0f, "%.2f");
    if (IsItemHovered())
        SetTooltip("Overlay opacity.");

    Checkbox("Only while playing", &m_OnlyWhilePlaying);
    if (IsItemHovered())
        SetTooltip("Only show the effect during a race.");
}

// =============================================================================
// Render - settings window
// =============================================================================
void LSDModule::Render()
{
}

// =============================================================================
// Settings
// =============================================================================
void LSDModule::SettingsInit(SettingMgr& Settings)
{
    Settings["Acid"]["Enable"].GetAsBool(&Enabled);
    Settings["Acid"]["Speed"].GetAsFloat(&m_Speed);
    Settings["Acid"]["Alpha"].GetAsFloat(&m_Alpha);
    Settings["Acid"]["OnlyWhilePlaying"].GetAsBool(&m_OnlyWhilePlaying);

    if (m_Speed < 0.05f || m_Speed > 5.0f) m_Speed = 0.4f;
    if (m_Alpha < 0.01f || m_Alpha > 1.0f) m_Alpha = 0.25f;
}

void LSDModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Acid"]["Enable"].Set(Enabled);
    Settings["Acid"]["Speed"].Set(m_Speed);
    Settings["Acid"]["Alpha"].Set(m_Alpha);
    Settings["Acid"]["OnlyWhilePlaying"].Set(m_OnlyWhilePlaying);
}
