#pragma once

#include "../../IModule.h"
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <unordered_set>
#include <random>
#include <deque>

// TrackMania Exchange integration (tmnf.exchange / tmuf.exchange), inspired by BigBang1112's
// Randomizer TMF (https://github.com/BigBang1112/randomizer-tmf) but implemented from scratch
// against TMX's own public API - no shared code, no GBX.NET parsing. Endpoints used:
//   GET https://{site}/api/tracks?count=N&fields=...&difficulty=..&primarytype=..&environment=..
//                                &routes=..&mood=..&name=..
//   GET https://{site}/trackgbx/{id}
// Each fetch attempt picks one value per active multi-select filter (TMX only takes one value
// per param), pulls a batch of results, and picks randomly among whatever passes the filters TMX
// can't do server-side (min/max author time, already-played). Goal completion
// is checked against GetRaceTime()/GetState(), compared to the medal thresholds TMX returns.
//
// Fetching runs on a background thread (FetchWorker) so the game doesn't stall on network calls.
// The worker only touches its own FetchFilters/FetchResult copies - the "live" m_Track* members
// are only ever written on the main thread in ApplyPendingResultIfAny(), so nothing else needs a
// mutex around them.
class RandomizerModule : public IModule
{
public:
    RandomizerModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "RMC";
        this->FancyName       = "RMC";
        this->Category        = "Track Tools";
    }
    ~RandomizerModule();

    virtual void RenderAnyways()  override;
    virtual void Render()         override;
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }

    // Read by TwinkDiscordRPModule for its "Open In Browser" button - the current track's TMX
    // page, e.g. https://tmnf.exchange/trackshow/13616986.
    bool HasCurrentTrack() const { return m_HasTrack; }
    std::string GetCurrentTmxUrl() const;
    // Raw (un-stripped) track name, so a caller can compare it against the in-game challenge name
    // to tell whether the player is actually *in* this track right now, not just that RMC has one
    // queued - see TwinkDiscordRPModule::UpdatePresence for why that distinction matters.
    std::string GetCurrentTrackName() const { return m_HasTrack ? m_TrackName : ""; }

private:
    int  m_GoalIdx        = 1;  // 0=Author 1=Gold 2=Silver 3=Bronze 4=Finished

    // Multi-select filters, each its own row of toggle buttons rather than a single dropdown, so
    // e.g. Stadium AND Snow can both be picked. Empty selection means "any of these". Enum
    // orderings match BigBang1112's EDifficulty/EEnvironment/EPrimaryType/ERoutes/EMood.cs.
    std::vector<bool> m_SiteSel        = { true, false }; // TMNF, TMUF
    std::vector<bool> m_DifficultySel  = std::vector<bool>(4, false); // Beginner/Intermediate/Expert/Lunatic
    std::vector<bool> m_GamemodeSel    = std::vector<bool>(6, false); // Race/Puzzle/Platform/Stunts/Shortcut/Laps
    std::vector<bool> m_EnvironmentSel = std::vector<bool>(7, false); // Snow/Desert/Rally/Island/Coast/Bay/Stadium
    std::vector<bool> m_RoutesSel      = std::vector<bool>(3, false); // Single/Multi/Symmetric
    std::vector<bool> m_MoodSel        = std::vector<bool>(4, false); // Sunrise/Day/Sunset/Night

    // Case-insensitive substring match against the track name - empty means no name filter.
    std::string m_NameContains;
    char        m_NameContainsBuf[128] = "";

    bool m_AutoProgress   = true;
    bool m_ShowOverlay    = true;
    bool m_AutoOpen       = true;
    bool m_ShowFilterWindow = true;
    bool m_ShowSearchProgress = false; // default off - see the "Checking IDs" toggle in Settings

    // Auto-skip if the goal isn't met within this many seconds of *actually racing*. Only time
    // spent in RaceState::Running counts - the "3,2,1,GO" countdown (including one re-entered via
    // a respawn/restart) pauses the clock instead of just delaying its start. m_TrackStartTime
    // still anchors CheckLoadFailure, which does want to measure from when the track went current.
    bool   m_EnableTimeout        = false;
    float  m_TimeoutSeconds       = 120.f;
    char   m_TimeoutHBuf[8] = "", m_TimeoutMBuf[8] = "", m_TimeoutSBuf[8] = ""; // H/M/S input boxes - see TimeHMSInput
    bool   m_TimeoutStarted       = false; // true once Running has been seen at least once
    double m_TimeoutAccumulatedSec = 0.0;   // seconds actually spent Running so far, this track
    std::chrono::steady_clock::time_point m_TimeoutLastTick;
    std::chrono::steady_clock::time_point m_TrackStartTime = std::chrono::steady_clock::now();

    // Some TMX maps need content the local install doesn't have - missing blocks (env-mixed
    // maps), TMUnlimiter, or a car/vehicle extension - and the game refuses to load them with its
    // own blocking popup ("Could not load challenge!" / "you must install the X extension"). TMX
    // has no metadata field for any of this, so rather than trying to predict it, this confirms
    // the load actually happened (in-game challenge name matches the fetched track, and a real
    // vehicle/race exists) within a short window, and skips to a different track if it never
    // does. Works the same regardless of which specific popup is blocking the load - it never
    // inspects the popup itself. See CheckLoadFailure().
    static constexpr float kLoadCheckSeconds = 12.f;
    bool m_LoadConfirmed = false;

    // Author-time range filter, in seconds (0 = no bound on that side)
    float m_MinAuthorTimeSec = 0.f;
    float m_MaxAuthorTimeSec = 0.f;
    char  m_MinAuthorHBuf[8] = "", m_MinAuthorMBuf[8] = "", m_MinAuthorSBuf[8] = "";
    char  m_MaxAuthorHBuf[8] = "", m_MaxAuthorMBuf[8] = "", m_MaxAuthorSBuf[8] = "";

    // Current track - only ever written by ApplyPendingResultIfAny() on the main thread
    bool        m_HasTrack     = false;
    int         m_TrackSiteIdx = 0; // which site (kSiteHosts index) this track actually came from
    long long   m_TrackId      = 0;
    std::string m_TrackName;
    long long   m_AuthorTime   = 0;
    long long   m_GoldTarget   = 0;
    long long   m_SilverTarget = 0;
    long long   m_BronzeTarget = 0;
    long long   m_Difficulty   = 0;
    std::string m_LocalPath;

    // Session stats (this game launch only, not persisted)
    int m_TracksPlayed  = 0;
    int m_TracksSkipped = 0;

    bool          m_GoalMet   = false;
    TM::RaceState m_LastState = TM::RaceState::Finished;

    std::string       m_StatusMsg;
    bool              m_StatusIsError = false;

    // Live fetch progress, updated by the worker thread and read every frame by the overlay -
    // plain atomics are fine, they're just counters.
    static constexpr int kMaxFetchAttempts = 20;
    std::atomic<int>  m_FetchAttempt{ 0 };
    std::atomic<bool> m_FetchDownloading{ false };

    // Track IDs already played to completion, persisted to RMC.ini next to the downloaded maps
    // so the same track doesn't come back up in a future session. Only touched from the main
    // thread; the fetch worker gets its own read-only snapshot via FetchFilters.playedIds.
    std::unordered_set<long long> m_PlayedTrackIds;
    std::string PlayedTrackIdsFilePath() const;
    void LoadPlayedTrackIds();
    void SavePlayedTrackIds();

    // Short-term repeat cooldown - the last few tracks served (played or not) are excluded too,
    // so the next fetch can't hand back something you were just on. Separate from
    // m_PlayedTrackIds (a permanent blacklist) and not persisted - it's about spacing within a
    // session, not remembering between sessions.
    static constexpr size_t kRecentCooldownCount = 5;
    std::deque<long long> m_RecentTrackIds;

    // A richer history log (name/site/when), deduplicated by (siteIdx, trackId) - a repeat visit
    // just moves the entry to the front and refreshes its timestamp. Capped so it can't grow
    // forever, but a favorited entry is exempt from that cap.
    struct HistoryEntry
    {
        int         siteIdx = 0;
        long long   trackId = 0;
        std::string trackName;
        long long   timestampUnix = 0;
        bool        favorite = false;
        std::string localPath; // where the .Challenge.Gbx last landed - "" if never downloaded
    };
    static constexpr size_t kMaxHistoryEntries = 150; // non-favorited entries only
    std::vector<HistoryEntry> m_History; // newest-first

    std::string m_HistorySearch;
    char        m_HistorySearchBuf[128] = "";
    bool        m_HistoryFavoritesOnly = false;

    std::string HistoryFilePath() const;
    void LoadHistory();
    void SaveHistory();
    void AddHistoryEntry(int siteIdx, long long trackId, const std::string& trackName, const std::string& localPath);
    void RenderHistoryTab();

    ImVec2 m_WndPos    = { 0.f, 0.f };
    bool   m_WndPosSet = false;

    // Playtime timers, ticked each frame using real elapsed time rather than "now - start" so
    // toggling Enabled off and back on doesn't count the gap. Both require Enabled and being
    // loaded into a track, so they tick together and never drift. Session resets each launch (or
    // via the reset button); total persists across restarts.
    bool   m_ShowSessionTimer = true;
    bool   m_ShowTotalTimer   = true;
    double m_SessionSeconds   = 0.0;
    double m_TotalSeconds     = 0.0;
    std::chrono::steady_clock::time_point m_LastTickTime = std::chrono::steady_clock::now();

    // Background fetch machinery
    struct FetchFilters
    {
        std::vector<bool> siteSel, difficultySel, gamemodeSel, environmentSel, routesSel, moodSel;
        float minAuthorSec = 0.f, maxAuthorSec = 0.f;
        std::string nameContains;
        std::unordered_set<long long> playedIds; // snapshot of m_PlayedTrackIds at fetch start
        std::unordered_set<long long> recentIds; // snapshot of m_RecentTrackIds at fetch start
    };

    struct FetchResult
    {
        bool        ok = false;
        std::string statusMsg;
        bool        statusIsError = false;
        int         siteIdx = 0; // which site this track was actually found on
        long long   trackId = 0;
        std::string trackName;
        long long   authorTime = 0, goldTarget = 0, silverTarget = 0, bronzeTarget = 0, difficulty = 0;
        std::string localPath;
    };

    // "Current" is what's shown/played; "Next" is prefetched as soon as Current is set, so
    // goal-completion/skip can swap instantly instead of waiting on a fresh fetch.
    enum class FetchTarget { Current, Next };
    FetchResult       m_NextTrack;
    bool              m_NextReady      = false;
    bool              m_AdvancePending = false; // advance requested while a fetch was in flight
    std::string       m_NextTrackFilterSnapshot; // filters in effect when m_NextTrack was fetched - see RequestNextTrack

    std::thread       m_FetchThread;
    std::mutex        m_ResultMutex;
    std::atomic<bool> m_ResultPending{ false };
    FetchResult       m_PendingResult;
    FetchTarget       m_PendingResultTarget = FetchTarget::Current; // guarded by m_ResultMutex

    // True whenever a fetch (current-direct or next-prefetch) is running. Only a Current fetch
    // should block the Fetch/Skip buttons - a Next-prefetch running in the background still
    // leaves a perfectly playable track active.
    std::atomic<bool> m_FetchInFlight{ false };
    FetchTarget       m_InFlightTarget = FetchTarget::Current;
    bool IsBlockingFetch() const { return m_FetchInFlight && m_InFlightTarget == FetchTarget::Current; }

    long long GoalThreshold() const;
    long long SessionSecondsPlayed() const { return (long long)m_SessionSeconds; }
    long long TotalSecondsPlayed()   const { return (long long)m_TotalSeconds; }
    void TickTimers();

    // Seconds left before the auto-skip timeout fires - the full budget while it hasn't started
    // counting yet, ticking down once it has.
    long long TimeoutSecondsLeft() const
    {
        if (!m_TimeoutStarted) return (long long)m_TimeoutSeconds;
        double left = (double)m_TimeoutSeconds - m_TimeoutAccumulatedSec;
        return left > 0.0 ? (long long)left : 0;
    }

    // GetPayingAccountType() reads real account state - "Disconnected" means no account session
    // is active yet (still on the game's login screen).
    bool IsLoggedIn() const { return Twinkie->GetPayingAccountType() != TM::AccountType::Disconnected; }

    bool HttpGet(const std::string& host, const std::string& path, std::vector<unsigned char>& outData, std::string& errorMsg);
    bool ParseAndFilterCandidate(const std::string& objJson, const FetchFilters& filters, FetchResult& out);
    bool SearchBatchFor(int siteIdx, const FetchFilters& filters, std::mt19937_64& rng, FetchResult& out);
    bool TrackNeedsExtension(int siteIdx, long long trackId);
    bool DownloadTrackFileFor(int siteIdx, long long trackId, const std::string& trackName, std::string& outLocalPath, std::string& errorMsg);
    void FetchWorker(FetchFilters filters, FetchTarget target);
    void StartFetch(FetchTarget target);
    void StartPrefetchNext();
    void PromoteNextToCurrent();
    void ApplyResultToCurrent(const FetchResult& result);
    std::string BuildFilterSnapshot() const;

    void RequestNextTrack();
    void SkipTrack();
    void CheckGoalCompletion();
    void CheckTimeout();
    void CheckLoadFailure();
    void ApplyPendingResultIfAny();
    void OpenDownloadedTrackInGame();
    void OpenTmxTrackPage();
    void OpenTrackFileInGame(const std::string& localPath) const;
    void OpenTmxUrl(int siteIdx, long long trackId) const;
    void RenderFilterControls();
    void RenderMedalTimes();
};
