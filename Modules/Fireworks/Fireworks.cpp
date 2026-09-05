#include "Fireworks.h"
#include <cmath>

// ── Internal helpers ──────────────────────────────────────────────────────────

void FireworksModule::Burst(float cx, float cy, float hue)
{
    std::uniform_real_distribution<float> angleDist(0.f, 3.14159265f * 2.f);
    std::uniform_real_distribution<float> speedDist(0.3f, 1.0f);
    std::uniform_real_distribution<float> lifeDist(0.8f, 1.2f);
    std::uniform_real_distribution<float> hueDist(-0.06f, 0.06f);
    std::uniform_real_distribution<float> sizeDist(0.6f, 1.4f);

    int count = m_ParticleCount / m_BurstCount;

    for (int i = 0; i < count; i++)
    {
        float angle = angleDist(m_Rng);
        float spd   = speedDist(m_Rng) * m_Speed;
        float h     = fmodf(hue + hueDist(m_Rng) + 1.f, 1.f);
        float r, g, b;
        ImGui::ColorConvertHSVtoRGB(h, 1.f, 1.f, r, g, b);

        FireParticle p;
        p.x       = cx;
        p.y       = cy;
        p.vx      = cosf(angle) * spd;
        p.vy      = sinf(angle) * spd;
        p.r       = r; p.g = g; p.b = b;
        p.maxLife = m_ParticleLife * lifeDist(m_Rng);
        p.life    = p.maxLife;
        p.size    = m_ParticleSize * sizeDist(m_Rng);
        m_Particles.push_back(p);
    }
}

void FireworksModule::TriggerFireworks()
{
    float W = ImGui::GetIO().DisplaySize.x;
    float H = ImGui::GetIO().DisplaySize.y;
    if (W <= 0 || H <= 0) { W = 1920.f; H = 1080.f; }

    m_Particles.reserve(m_Particles.size() + m_ParticleCount);

    std::uniform_real_distribution<float> xDist(W * 0.15f, W * 0.85f);
    std::uniform_real_distribution<float> yDist(H * 0.15f, H * 0.55f);

    float hueStep = 1.f / (float)m_BurstCount;
    std::uniform_real_distribution<float> hueStart(0.f, 1.f);
    float baseHue = hueStart(m_Rng);

    for (int b = 0; b < m_BurstCount; b++)
    {
        float hue = fmodf(baseHue + b * hueStep, 1.f);
        Burst(xDist(m_Rng), yDist(m_Rng), hue);
    }
}

// ── RenderAnyways ─────────────────────────────────────────────────────────────

void FireworksModule::RenderAnyways()
{
    if (!Enabled) return;

    // Delta time
    auto  now = std::chrono::steady_clock::now();
    float dt  = m_FirstFrame ? 0.f
              : std::chrono::duration<float>(now - m_LastTick).count();
    if (dt > 0.1f) dt = 0.1f;
    m_FirstFrame = false;
    m_LastTick   = now;

    // ── PB detection ─────────────────────────────────────────────────────────
    bool playing = Twinkie->IsPlaying() && !Twinkie->IsInEditor();
    TM::RaceState state = playing ? Twinkie->GetState() : TM::RaceState::BeforeStart;

    if (m_LastState == TM::RaceState::BeforeStart && state == TM::RaceState::Running)
    {
        m_BestAtRunStart  = playing ? Twinkie->GetBestTime() : -1;
        m_RaceActuallyRan = false;
    }

    if (state == TM::RaceState::Running && playing && Twinkie->GetRaceTime() >= 0)
        m_RaceActuallyRan = true;

    if (m_LastState == TM::RaceState::Running
        && state     == TM::RaceState::Finished
        && playing)
    {
        int  newBest   = Twinkie->GetBestTime();
        bool usesScore = Twinkie->ChallengeUsesScore();
        bool isPB = (m_BestAtRunStart <= 0 && newBest > 0)
                 || (m_BestAtRunStart > 0 &&
                     (!usesScore ? (newBest > 0 && newBest < m_BestAtRunStart)
                                 : (newBest > m_BestAtRunStart)));
        if (isPB)
            TriggerFireworks();
    }

    m_LastState = state;

    // ── Test trigger ─────────────────────────────────────────────────────────
    if (m_TriggerTest)
    {
        m_TriggerTest = false;
        TriggerFireworks();
    }

    // ── Update + draw particles ───────────────────────────────────────────────
    if (m_Particles.empty()) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    for (auto& p : m_Particles)
    {
        p.vy   += m_Gravity * dt;
        p.x    += p.vx * dt;
        p.y    += p.vy * dt;
        p.life -= dt;

        float alpha = p.life / p.maxLife;
        if (alpha < 0.f) alpha = 0.f;

        ImU32 col = IM_COL32(
            (int)(p.r * 255),
            (int)(p.g * 255),
            (int)(p.b * 255),
            (int)(alpha * 255));

        dl->AddCircleFilled(ImVec2(p.x, p.y), p.size * alpha, col);
    }

    // Remove dead particles
    m_Particles.erase(
        std::remove_if(m_Particles.begin(), m_Particles.end(),
            [](const FireParticle& p) { return p.life <= 0.f; }),
        m_Particles.end());
}

// ── RenderSettings ────────────────────────────────────────────────────────────

void FireworksModule::RenderSettings()
{
    using namespace ImGui;

    SliderInt("Particles per trigger##fw", &m_ParticleCount, 40, 600);
    SliderInt("Burst count##fw",           &m_BurstCount,    1,  8);
    SliderFloat("Particle speed##fw",      &m_Speed,         100.f, 900.f, "%.0f");
    SliderFloat("Gravity##fw",             &m_Gravity,       0.f,   800.f, "%.0f");
    SliderFloat("Particle lifetime##fw",   &m_ParticleLife,  0.5f,  5.f,   "%.1f s");
    SliderFloat("Particle size##fw",       &m_ParticleSize,  1.f,   12.f,  "%.1f px");

    Spacing();
    if (Button("Test fireworks"))
        m_TriggerTest = true;
}

// ── Render (settings window) ──────────────────────────────────────────────────

void FireworksModule::Render()
{
}

// ── RenderMenuItem ────────────────────────────────────────────────────────────

void FireworksModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_FIRE " PB Fireworks", "", Enabled))
        Enabled = !Enabled;
}

// ── Settings persistence ──────────────────────────────────────────────────────

void FireworksModule::SettingsInit(SettingMgr& Settings)
{
    Settings["Fireworks"]["Enable"].GetAsBool(&Enabled);
    Settings["Fireworks"]["ParticleCount"].GetAsInt(&m_ParticleCount);
    Settings["Fireworks"]["BurstCount"].GetAsInt(&m_BurstCount);
    Settings["Fireworks"]["Speed"].GetAsFloat(&m_Speed);
    Settings["Fireworks"]["Gravity"].GetAsFloat(&m_Gravity);
    Settings["Fireworks"]["ParticleLife"].GetAsFloat(&m_ParticleLife);
    Settings["Fireworks"]["ParticleSize"].GetAsFloat(&m_ParticleSize);
}

void FireworksModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Fireworks"]["Enable"].Set(Enabled);
    Settings["Fireworks"]["ParticleCount"].Set(m_ParticleCount);
    Settings["Fireworks"]["BurstCount"].Set(m_BurstCount);
    Settings["Fireworks"]["Speed"].Set(m_Speed);
    Settings["Fireworks"]["Gravity"].Set(m_Gravity);
    Settings["Fireworks"]["ParticleLife"].Set(m_ParticleLife);
    Settings["Fireworks"]["ParticleSize"].Set(m_ParticleSize);
}
