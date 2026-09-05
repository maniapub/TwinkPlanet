#pragma once

#include "../../IModule.h"
#include <vector>
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

// A standalone TrackMania Exchange browser - search tmnf.exchange/tmuf.exchange by name, see each
// result's thumbnail (GET https://{site}/trackshow/{id}/image/1, the same image used for the
// track page's og:image preview), download+launch a map directly ("Play Map"), or list a map's
// top 10 replays and download any of them ("View Replays"). Independent of RMC - this is for
// looking up a specific map by name rather than getting served a random one.
//
// Uses the same TMX endpoints RMC does (/api/tracks, /api/replays, /trackgbx/{id},
// /recordgbx/{id}) but keeps its own copies of the small JSON/HTTP/WIC helpers rather than
// sharing Randomizer's.
class TmxBrowserModule : public IModule
{
public:
    TmxBrowserModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "TmxBrowser";
        this->FancyName       = "TMX";
    }
    ~TmxBrowserModule();

    // No RenderMenuItem() override - same as AboutModule's pattern, this stays out of the
    // generic "Modules" menu since it gets its own dedicated top-level menu-bar entry instead
    // (TwinkUi.cpp, right next to "Modules"/"Custom"), so it isn't buried a level deep.
    virtual void Render()         override;
    virtual void RenderAnyways()  override {}
    virtual void RenderSettings() override;
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return true; }
    virtual bool HasMenuEntry()   override { return false; } // reached via its own top-level menu-bar entry instead

private:
    struct SearchResult
    {
        int         siteIdx = 1;
        long long   trackId = 0;
        std::string trackName;
        long long   gamemode   = -1; // PrimaryType, see kGamemodeNames; -1 = unknown (e.g. a favorite that hasn't been re-searched)
        long long   difficulty = -1; // see kDifficultyNames; -1 = unknown
        void*       thumbTex          = nullptr; // IDirect3DTexture9*, opaque here to avoid pulling d3d9.h into every user of this header
        bool        thumbRequested    = false;
        bool        thumbFailed       = false;
    };

    struct FavoriteEntry
    {
        int         siteIdx = 1;
        long long   trackId = 0;
        std::string trackName;
    };

    struct ReplayResult
    {
        long long   replayId = 0;
        long long   scoreMs  = 0;
        int         position = 0; // 0 = fastest
        std::string localPath;    // set once downloaded
    };

    int  m_SiteIdx = 1; // 0=TMNF, 1=TMUF - matches Randomizer's own kSiteHosts ordering
    std::string m_Query;
    char        m_QueryBuf[128] = "";

    // How much bigger the hover-preview tooltip is than the small in-list thumbnail (e.g. 4.0 =
    // 4x). User-adjustable in Settings.
    float m_HoverPreviewScale = 4.f;

    // Filters - single-select only (TMX's public API accepts one concrete value per param, not a
    // list - same limitation Randomizer.h's own filters ran into). Index 0 is always "Any" in
    // each of these; a real filter value is comboIndex-1. Enum orderings match Randomizer.h's own
    // (BigBang1112's EPrimaryType/EEnvironment/ERoutes/EMood/EDifficulty.cs).
    int m_FilterGamemode   = 0;
    int m_FilterEnvironment = 0;
    int m_FilterRoutes     = 0;
    int m_FilterMood       = 0;
    int m_FilterDifficulty = 0;

    std::vector<SearchResult> m_Results;
    std::string m_SearchStatus;
    bool        m_SearchIsError = false;

    // Favorites - persisted separately from search results, so a favorited map stays findable
    // regardless of what's currently searched. Checking "Favorites" swaps m_Results to show these
    // instead of running a live search (no thumbnail re-fetch needed - QueueThumbnail already
    // caches to disk by (site, trackId), so a previously-seen favorite's thumbnail loads instantly).
    std::vector<FavoriteEntry> m_Favorites;
    bool m_ShowFavoritesOnly = false;
    bool m_FavoritesNeedRefresh = false; // deferred - toggling a favorite mid-render must not mutate m_Results while its own loop is iterating it
    std::string FavoritesFilePath() const;
    void LoadFavorites();
    void SaveFavorites();
    bool IsFavorite(int siteIdx, long long trackId) const;
    void ToggleFavorite(int siteIdx, long long trackId, const std::string& trackName);
    void ShowFavoritesInResults();

    // Pagination - TMX's /api/tracks only supports a forward cursor ("after"/"from": passing the
    // last result's TrackId returns the next batch). There's no way to reverse that cursor
    // directly (a "before" param exists but returns a different, unrelated set of results, not a
    // real inverse), so Previous is done client-side instead: every page's cursor gets remembered
    // here as it's visited, and going back just re-fetches an earlier page using the same cursor
    // that was used to reach it the first time, rather than trusting the API to reverse itself.
    std::vector<long long> m_PageCursors{ 0 }; // m_PageCursors[i] = the "after" value used for page i (0 = first page, no cursor)
    int  m_CurrentPage = 0;
    bool m_HasMorePages = false; // TMX's own "More" flag from the last response

    int  m_SelectedIdx = -1; // index into m_Results whose replays are being viewed
    std::vector<ReplayResult> m_Replays;
    std::string m_ReplaysStatus;
    bool        m_ReplaysIsError = false;

    // Search
    std::thread       m_SearchThread;
    std::atomic<bool> m_SearchInFlight{ false };
    std::mutex        m_SearchResultMutex;
    std::atomic<bool> m_SearchResultPending{ false };
    std::vector<SearchResult> m_PendingResults;
    std::string       m_PendingSearchStatus;
    bool              m_PendingSearchIsError = false;
    bool              m_PendingHasMorePages = false;

    // Replay list
    std::thread       m_ReplayThread;
    std::atomic<bool> m_ReplayInFlight{ false };
    std::mutex        m_ReplayResultMutex;
    std::atomic<bool> m_ReplayResultPending{ false };
    std::vector<ReplayResult> m_PendingReplays;
    std::string       m_PendingReplaysStatus;
    bool              m_PendingReplaysIsError = false;
    long long         m_ReplaysForTrackId = 0; // guards a late reply landing after a different map got picked
    int               m_ReplaysSiteIdx = 1; // which site the currently-shown replay list is for

    // "Play Map" (download + launch)
    std::thread       m_PlayThread;
    std::atomic<bool> m_PlayInFlight{ false };
    std::mutex        m_PlayResultMutex;
    std::atomic<bool> m_PlayResultPending{ false };
    std::string       m_PendingPlayLocalPath;
    bool              m_PendingPlayOk = false;
    std::string       m_PlayStatus;
    bool              m_PlayIsError = false;

    // Thumbnails - one persistent worker thread pulling from a small queue, since a search can
    // want a dozen-plus thumbnails at once and spawning that many ad-hoc threads felt excessive.
    struct ThumbRequest { int siteIdx; long long trackId; };
    struct ThumbReady    { int siteIdx; long long trackId; std::string localPath; };
    std::thread          m_ThumbThread;
    std::atomic<bool>    m_ThumbThreadRunning{ false };
    std::mutex           m_ThumbQueueMutex;
    std::vector<ThumbRequest> m_ThumbQueue;
    std::mutex           m_ThumbReadyMutex;
    std::vector<ThumbReady>   m_ThumbReadyQueue;

    bool HttpGet(const std::string& host, const std::string& path, std::vector<unsigned char>& outData, std::string& errorMsg);
    std::string DownloadDir(const char* subfolder) const;

    void StartSearch();       // fresh search - resets paging to page 0
    void StartSearchPage(int direction); // +1 = next, -1 = previous
    void SearchWorker(int siteIdx, std::string query, long long afterCursor,
        int gamemodeSel, int environmentSel, int routesSel, int moodSel, int difficultySel);
    void ApplyPendingSearchIfAny();

    void StartReplaySearch(int siteIdx, long long trackId);
    void ReplayWorker(int siteIdx, long long trackId);
    void ApplyPendingReplaysIfAny();

    void StartPlayMap(int siteIdx, long long trackId, std::string trackName);
    void PlayMapWorker(int siteIdx, long long trackId, std::string trackName);
    void ApplyPendingPlayIfAny();
    void OpenTrackFileInGame(const std::string& localPath) const;

    void DownloadReplayToDisk(int siteIdx, long long replayId, const std::string& trackName, int rank);
    // Downloads the map too (fresh, same as Play Map) before the replay and then opens the replay
    // in-game - a replay only references its challenge by UID/name, it doesn't embed the map
    // itself, so TMF needs that challenge already present locally to actually load and play the
    // ghost correctly rather than erroring or silently failing.
    void ViewReplayInGame(int siteIdx, long long trackId, long long replayId, const std::string& trackName, int rank);
    bool IsLoggedIn() const;

    void QueueThumbnail(int siteIdx, long long trackId);
    void ThumbWorkerLoop();
    void ApplyPendingThumbsIfAny();
    void ReleaseAllThumbTextures();

    std::string TmxUrl(int siteIdx, long long trackId) const;
};
