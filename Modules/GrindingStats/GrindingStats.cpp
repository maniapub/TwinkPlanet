#include "GrindingStats.h"
#include <sstream>
#include <iomanip>

// Twinkie->FormatTmDuration() only takes an unsigned int (ms) - fine for a single race, but this
// module accumulates playtime across many sessions on one map, which can run well past the
// ~49.7-day wraparound point a 32-bit millisecond count hits. This mirrors that function's own
// H:MM:SS.ms formatting but keeps the full 64-bit range.
static std::string FormatDurationMs64(unsigned long long durationMs)
{
    unsigned long long totalSeconds = durationMs / 1000;
    unsigned long long millis  = (durationMs % 1000) / 10;
    unsigned long long seconds = totalSeconds % 60;
    unsigned long long minutes = (totalSeconds / 60) % 60;
    unsigned long long hours   = totalSeconds / 3600;

    std::ostringstream ss;
    ss << std::setfill('0');
    if (hours > 0) ss << std::setw(2) << hours << ":";
    ss << std::setw(2) << minutes << ":" << std::setw(2) << seconds << "." << std::setw(2) << millis;
    return ss.str();
}

void GrindingStatsModule::RenderMenuItem()
{
	using namespace ImGui;

	if (MenuItem(ICON_FK_CALENDAR " Grinding Stats", "", Enabled))
	{
		Enabled = !Enabled;
	}
}

Stat& GrindingStatsModule::GetCurrentTotalStat()
{
	return AllStats[LastLoadedChallengeUID];
}

void GrindingStatsModule::SettingsInit(SettingMgr& Settings)
{
	Tab& GrindingStatsSection = Settings["Grinding Stats"];
	 
	for (Setting& SettingValue : GrindingStatsSection.Settings)
	{
		if (!SettingValue.Name.starts_with("U_")) continue;
		std::string KeyToAccess = SettingValue.Name.substr(2);
		Stat NewStat = {};
		NewStat.FromString(SettingValue.Value);
		AllStats[KeyToAccess] = NewStat;
	}

	GrindingStatsSection["Enable UI"].GetAsBool(&Enabled);
	GrindingStatsSection["Background color"].GetAsVec4(&ColorBackground);
	GrindingStatsSection["Text color"].GetAsVec4(&ColorText);
}

void GrindingStatsModule::SettingsSave(SettingMgr& Settings)
{
	auto& GrindingStatsSection = Settings["Grinding Stats"];

	for (auto& Pair : AllStats)
	{
		GrindingStatsSection["U_" + Pair.first].Value = Pair.second;
	}

	GrindingStatsSection["Enable UI"].Set(Enabled);
	GrindingStatsSection["Background color"].Set(ColorBackground);
	GrindingStatsSection["Text color"].Set(ColorText);
}

void GrindingStatsModule::RenderInactive()
{
	if (Twinkie->TMInterfaceLoaded) return;

	CurrentChallenge = Twinkie->GetChallenge();

	if (!Twinkie->IsPlaying())
	{
		if (PreviousChallenge != CurrentChallenge and CurrentChallenge == 0)
		{
			OnMapUnload();
		}
		if (PreviousChallenge != CurrentChallenge and CurrentChallenge != 0)
		{
			LastLoadedChallengeUID = Twinkie->GetChallengeUID();
			OnMapLoad();
		}
		PreviousChallenge = CurrentChallenge;
		return;
	}

	LastLoadedChallengeUID = Twinkie->GetChallengeUID();
	CurrentRaceTime = Twinkie->GetRaceTime();
	CurrentSignedRaceTime = Twinkie->GetSignedRaceTime();
	CurrentRespawnCount = Twinkie->GetRespawns();
	CurrentState = Twinkie->GetState();

	if (CurrentRaceTime != 0)
	{
		LastNonZeroUnsignedRaceTime = CurrentRaceTime;
	}

	if (PreviousChallenge != CurrentChallenge)
	{
		OnMapLoad();
	}

	// if (PreviousSignedRaceTime != CurrentSignedRaceTime and CurrentSignedRaceTime < 0 and PreviousSignedRaceTime > CurrentSignedRaceTime and CurrentSignedRaceTime != -1)
	if (PreviousState != CurrentState and CurrentState == TM::BeforeStart)
	{
		OnReset();
	}

	if (PreviousRespawnCount != CurrentRespawnCount and CurrentRespawnCount > PreviousRespawnCount)
	{
		OnRespawn();
	}

	if (PreviousState != CurrentState and CurrentState == TM::Finished)
	{
		OnFinish();
	}

	PreviousRaceTime = CurrentRaceTime;
	PreviousSignedRaceTime = CurrentSignedRaceTime;
	PreviousChallenge = CurrentChallenge;
	PreviousRespawnCount = CurrentRespawnCount;
	PreviousState = CurrentState;
}

void GrindingStatsModule::RenderAnyways()
{
	if (!Twinkie->IsPlaying()) return;
	if (Twinkie->IsInEditor()) return;

	using namespace ImGui;

	unsigned long long TotalPlaytime = GetCurrentTotalStat().Playtime;
	unsigned long long CurrentPlaytime = CurrentStat.Playtime;

	if (!CurrentState == TM::BeforeStart)
	{
		TotalPlaytime += CurrentRaceTime;
		CurrentPlaytime += CurrentRaceTime;
	}

	auto WindowFlags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize;
	if (!*UiRenderEnabled) WindowFlags |= ImGuiWindowFlags_NoInputs;

	PushStyleColor(ImGuiCol_WindowBg, ColorBackground);
	PushStyleColor(ImGuiCol_Text, ColorText);
	Begin("##GrindingStats", nullptr, WindowFlags);

	BeginDisabled(Twinkie->TMInterfaceLoaded);

	Text(ICON_FK_CLOCK_O " Total time: %s", FormatDurationMs64(TotalPlaytime).c_str());
	Text(ICON_FK_PLAY_CIRCLE_O " Session time: %s", FormatDurationMs64(CurrentPlaytime).c_str());
	Text(ICON_FK_FLAG_CHECKERED " Finishes: %d / %d", CurrentStat.Finishes, GetCurrentTotalStat().Finishes);
	Text(ICON_FK_REPEAT " Attempts: %d / %d", CurrentStat.Attempts, GetCurrentTotalStat().Attempts);
	Text(ICON_FK_REFRESH " Respawns: %d / %d / %d", Twinkie->GetRespawns(), CurrentStat.Respawns, GetCurrentTotalStat().Respawns);

	EndDisabled();

	End();

	PopStyleColor(2);
}

void GrindingStatsModule::RenderSettings()
{
	using namespace ImGui;
	ColorEdit4("Background color", &ColorBackground.x, ImGuiColorEditFlags_NoInputs);
	ColorEdit4("Text color", &ColorText.x, ImGuiColorEditFlags_NoInputs);
}

void GrindingStatsModule::OnReset()
{
	GetCurrentTotalStat().Attempts++;
	CurrentStat.Attempts++;
	GetCurrentTotalStat().Playtime += LastNonZeroUnsignedRaceTime;
	CurrentStat.Playtime += LastNonZeroUnsignedRaceTime;
}

void GrindingStatsModule::OnRespawn()
{
	GetCurrentTotalStat().Respawns++;
	CurrentStat.Respawns++;
}

void GrindingStatsModule::OnMapLoad()
{
	LastNonZeroUnsignedRaceTime = 0;
	CurrentStat = { 0, 0, 0, 0 };
}

void GrindingStatsModule::OnMapUnload()
{
	CurrentStat = { 0, 0, 0, 0 };
	GetCurrentTotalStat().Playtime += CurrentRaceTime;
}

void GrindingStatsModule::OnFinish()
{
	GetCurrentTotalStat().Finishes++;
	CurrentStat.Finishes++;
}