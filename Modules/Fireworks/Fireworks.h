#pragma once
#pragma execution_character_set("utf-8")

#include "../../IModule.h"
#include <vector>
#include <random>
#include <chrono>

struct FireParticle
{
    float x, y;       // position
    float vx, vy;     // velocity (px/s)
    float r, g, b;    // color
    float life;       // remaining lifetime (seconds)
    float maxLife;    // total lifetime (seconds)
    float size;       // circle radius (px)
};

class FireworksModule : public IModule
{
public:
    FireworksModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "Fireworks";
        this->FancyName       = "PB Fireworks";
        this->Category        = "Visual Effects";
        Enabled               = true;
    }

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings() override { return true; }

private:
    std::vector<FireParticle> m_Particles;

    // PB detection
    TM::RaceState m_LastState      = TM::RaceState::BeforeStart;
    int           m_BestAtRunStart = -1;
    bool          m_RaceActuallyRan = false;

    // Timing
    std::chrono::steady_clock::time_point m_LastTick;
    bool m_FirstFrame = true;

    // Settings
    int   m_ParticleCount = 200;   // particles per burst
    int   m_BurstCount    = 4;     // simultaneous bursts
    float m_Gravity       = 350.f; // px/s²
    float m_Speed         = 450.f; // initial velocity scale
    float m_ParticleLife  = 2.2f;  // seconds
    float m_ParticleSize  = 4.f;   // radius px

    // Test trigger flag (set by UI button, consumed in RenderAnyways)
    bool  m_TriggerTest   = false;

    std::mt19937 m_Rng{ std::random_device{}() };

    void Burst(float cx, float cy, float hue);
    void TriggerFireworks();
};
