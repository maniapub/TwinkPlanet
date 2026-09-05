#include "TmxBrowser.h"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <cstring>
#include <random>
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <wincodec.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "windowscodecs.lib")

namespace Filesystem = std::filesystem;

static const char* kSiteNames[2] = { "TMNF", "TMUF" };
static const char* kSiteHosts[2] = { "tmnf.exchange", "tmuf.exchange" };
static const char* kFields = "TrackId,TrackName,PrimaryType,Difficulty";

// Same enum orderings as Randomizer.h uses (BigBang1112's EPrimaryType.cs/EDifficulty.cs) - the
// site's own search table has Author/Tags/Length/Awards/Comments columns too, but none of those
// are exposed by TMX's public API, so Type and Difficulty are the closest match achievable here.
static const char* kGamemodeNames[6]   = { "Race", "Puzzle", "Platform", "Stunts", "Shortcut", "Laps" };
static const char* kDifficultyNames[4] = { "Beginner", "Intermediate", "Expert", "Lunatic" };

// "Any"-prefixed combo lists for the filter dropdowns - index 0 is always "Any" (no filter sent),
// so the real enum value for a selected combo index i is (i-1). Environment has an off-by-one
// against the field of the same name when *reading* results back (param N -> field N+1) - not
// relevant here since these are write-only filters, sent as our own 0-indexed value.
static const char* kAnyGamemodeNames[5]    = { "Any", "Race", "Puzzle", "Platform", "Stunts" };
static const char* kAnyEnvironmentNames[8] = { "Any", "Snow", "Desert", "Rally", "Island", "Coast", "Bay", "Stadium" };
static const char* kAnyRoutesNames[4]      = { "Any", "Single", "Multi", "Symmetric" };
static const char* kAnyMoodNames[5]        = { "Any", "Sunrise", "Day", "Sunset", "Night" };
static const char* kAnyDifficultyNames[5]  = { "Any", "Beginner", "Intermediate", "Expert", "Lunatic" };

// =============================================================================
// Small hand-rolled JSON helpers - not shared with Randomizer.cpp's copies, kept local here.
// =============================================================================
static bool JsonFindInt(const std::string& json, const std::string& key, long long& outVal)
{
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos += pattern.size();

    bool neg = false;
    if (pos < json.size() && json[pos] == '-') { neg = true; pos++; }

    long long val = 0;
    bool any = false;
    while (pos < json.size() && isdigit((unsigned char)json[pos]))
    {
        val = val * 10 + (json[pos] - '0');
        pos++;
        any = true;
    }
    if (!any) return false;
    outVal = neg ? -val : val;
    return true;
}

static bool JsonFindString(const std::string& json, const std::string& key, std::string& outVal)
{
    std::string pattern = "\"" + key + "\":\"";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos += pattern.size();

    std::string result;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            pos++;
            switch (json[pos])
            {
                case 'n':  result += '\n'; break;
                case 't':  result += '\t'; break;
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                default:   result += json[pos]; break;
            }
        }
        else
        {
            result += json[pos];
        }
        pos++;
    }
    outVal = result;
    return true;
}

static bool JsonResultsEmpty(const std::string& json)
{
    return json.find("\"Results\":[]") != std::string::npos || json.find("\"Results\":[") == std::string::npos;
}

// Splits the "Results":[{...},{...},...] array into individual object substrings by brace depth,
// same approach as Randomizer.cpp's own copy - JsonFindInt/JsonFindString only find the *first*
// match, so a multi-result batch needs splitting first.
static std::vector<std::string> JsonSplitResultsObjects(const std::string& json)
{
    std::vector<std::string> out;
    std::string pattern = "\"Results\":[";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return out;
    pos += pattern.size();

    int depth = 0;
    size_t objStart = std::string::npos;
    for (size_t i = pos; i < json.size(); i++)
    {
        char c = json[i];
        if (c == '{')
        {
            if (depth == 0) objStart = i;
            depth++;
        }
        else if (c == '}')
        {
            depth--;
            if (depth == 0 && objStart != std::string::npos)
            {
                out.push_back(json.substr(objStart, i - objStart + 1));
                objStart = std::string::npos;
            }
        }
        else if (c == ']' && depth == 0)
        {
            break;
        }
    }
    return out;
}

static std::string UrlEncode(const std::string& s)
{
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out.push_back((char)c);
        else
        {
            out.push_back('%');
            out.push_back(hex[c >> 4]);
            out.push_back(hex[c & 0xF]);
        }
    }
    return out;
}

static std::string SanitizeForFilename(const std::string& name)
{
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (isalnum((unsigned char)c) || c == ' ' || c == '-' || c == '_')
            out.push_back(c);
    }
    if (out.empty()) out = "track";
    if (out.size() > 80) out.resize(80);
    return out;
}

static std::string FormatTimeHMSMs(long long ms)
{
    if (ms < 0) ms = 0;
    long long cs = (ms / 10) % 100;
    long long totalSec = ms / 1000;
    long long s = totalSec % 60;
    long long m = (totalSec / 60) % 60;
    long long h = totalSec / 3600;
    char buf[32];
    if (h > 0) snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%02lld", h, m, s, cs);
    else       snprintf(buf, sizeof(buf), "%lld:%02lld.%02lld", m, s, cs);
    return buf;
}

// =============================================================================
// HTTP - same WinHTTP-over-HTTPS pattern as Randomizer.cpp's own copy.
// =============================================================================
bool TmxBrowserModule::HttpGet(const std::string& host, const std::string& path, std::vector<unsigned char>& outData, std::string& errorMsg)
{
    int wlenHost = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring whost(wlenHost, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, whost.data(), wlenHost);
    if (!whost.empty() && whost.back() == L'\0') whost.pop_back();

    int wlenPath = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlenPath, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlenPath);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    HINTERNET hSess = WinHttpOpen(L"TwinkieTmxBrowser/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) { errorMsg = "WinHTTP init failed"; return false; }

    HINTERNET hConn = WinHttpConnect(hSess, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); errorMsg = "Connect failed"; return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", wpath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq)
    {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
        errorMsg = "Open request failed"; return false;
    }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
        errorMsg = "HTTP request failed";
        return false;
    }

    DWORD statusCode = 0, statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    DWORD avail = 0, read = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
    {
        size_t off = outData.size();
        outData.resize(off + avail);
        WinHttpReadData(hReq, outData.data() + off, avail, &read);
        outData.resize(off + read);
    }
    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);

    if (statusCode != 200)
    {
        errorMsg = "HTTP status " + std::to_string(statusCode);
        return false;
    }
    return true;
}

std::string TmxBrowserModule::DownloadDir(const char* subfolder) const
{
    std::string docs = GetDocumentsFolder();
    Filesystem::path dir = Filesystem::path(docs) / "TrackMania" / "Tracks" / subfolder / "TMX";
    std::error_code ec;
    Filesystem::create_directories(dir, ec);
    return dir.string();
}

std::string TmxBrowserModule::TmxUrl(int siteIdx, long long trackId) const
{
    return "https://" + std::string(kSiteHosts[siteIdx]) + "/trackshow/" + std::to_string(trackId);
}

// =============================================================================
// Favorites - one line per entry ("siteIdx\x1ftrackId\x1ftrackName"), same \x1f-delimited
// approach as Randomizer.cpp's own history log, for the same reason: the name (last field) can
// then safely contain anything.
// =============================================================================
std::string TmxBrowserModule::FavoritesFilePath() const
{
    Filesystem::path dir = Filesystem::path(DownloadDir("Challenges"));
    return (dir / "Favorites.ini").string();
}

void TmxBrowserModule::LoadFavorites()
{
    m_Favorites.clear();

    std::ifstream in(FavoritesFilePath());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;
        size_t p1 = line.find('\x1f');
        size_t p2 = p1 == std::string::npos ? std::string::npos : line.find('\x1f', p1 + 1);
        if (p2 == std::string::npos) continue;

        FavoriteEntry f;
        try
        {
            f.siteIdx = std::stoi(line.substr(0, p1));
            f.trackId = std::stoll(line.substr(p1 + 1, p2 - p1 - 1));
        }
        catch (...) { continue; }
        f.trackName = line.substr(p2 + 1);
        m_Favorites.push_back(std::move(f));
    }
}

void TmxBrowserModule::SaveFavorites()
{
    std::ofstream out(FavoritesFilePath(), std::ios::trunc);
    if (!out.is_open()) return;
    for (auto& f : m_Favorites)
        out << f.siteIdx << '\x1f' << f.trackId << '\x1f' << f.trackName << "\n";
}

bool TmxBrowserModule::IsFavorite(int siteIdx, long long trackId) const
{
    for (auto& f : m_Favorites)
        if (f.siteIdx == siteIdx && f.trackId == trackId) return true;
    return false;
}

void TmxBrowserModule::ToggleFavorite(int siteIdx, long long trackId, const std::string& trackName)
{
    for (auto it = m_Favorites.begin(); it != m_Favorites.end(); ++it)
    {
        if (it->siteIdx == siteIdx && it->trackId == trackId)
        {
            m_Favorites.erase(it);
            SaveFavorites();
            if (m_ShowFavoritesOnly) m_FavoritesNeedRefresh = true;
            return;
        }
    }

    FavoriteEntry f;
    f.siteIdx = siteIdx;
    f.trackId = trackId;
    f.trackName = trackName;
    m_Favorites.push_back(f);
    SaveFavorites();
    if (m_ShowFavoritesOnly) m_FavoritesNeedRefresh = true;
}

// Swaps m_Results to show the favorites list instead of running a live search - no network call
// needed, thumbnails are already cached to disk from whenever each one was first seen.
void TmxBrowserModule::ShowFavoritesInResults()
{
    ReleaseAllThumbTextures();
    m_Results.clear();
    for (auto& f : m_Favorites)
    {
        SearchResult r;
        r.siteIdx   = f.siteIdx;
        r.trackId   = f.trackId;
        r.trackName = f.trackName;
        m_Results.push_back(r);
    }
    m_SearchStatus  = std::to_string(m_Results.size()) + " favorite" + (m_Results.size() == 1 ? "" : "s") + ".";
    m_SearchIsError = false;
    m_SelectedIdx   = -1;
    m_Replays.clear();

    for (auto& r : m_Results) QueueThumbnail(r.siteIdx, r.trackId);
}

// =============================================================================
// Search
// =============================================================================
void TmxBrowserModule::StartSearch()
{
    if (m_SearchInFlight) return;

    m_ShowFavoritesOnly = false;
    m_SearchInFlight = true;
    m_SearchStatus = m_Query.empty() ? "Loading recently uploaded maps..." : "Searching...";
    m_SearchIsError = false;
    m_SelectedIdx = -1;
    m_Replays.clear();
    m_PageCursors = { 0 };
    m_CurrentPage = 0;

    if (m_SearchThread.joinable()) m_SearchThread.join();
    m_SearchThread = std::thread(&TmxBrowserModule::SearchWorker, this, m_SiteIdx, m_Query, 0,
        m_FilterGamemode, m_FilterEnvironment, m_FilterRoutes, m_FilterMood, m_FilterDifficulty);
}

// Next needs the current page's last result to know where to continue from, so it's only valid
// once a page is actually loaded; Previous just re-fetches an earlier page using the cursor that
// was already used to reach it the first time - see m_PageCursors' comment for why (TMX has no
// way to reverse its own "after" cursor directly).
void TmxBrowserModule::StartSearchPage(int direction)
{
    if (m_SearchInFlight || m_ShowFavoritesOnly) return;

    long long afterCursor = 0;
    if (direction > 0)
    {
        if (!m_HasMorePages || m_Results.empty()) return;
        afterCursor = m_Results.back().trackId;
        if ((int)m_PageCursors.size() == m_CurrentPage + 1) m_PageCursors.push_back(afterCursor);
        m_CurrentPage++;
    }
    else
    {
        if (m_CurrentPage == 0) return;
        m_CurrentPage--;
        afterCursor = m_PageCursors[m_CurrentPage];
    }

    m_SearchInFlight = true;
    m_SearchStatus = m_Query.empty() ? "Loading recently uploaded maps..." : "Searching...";
    m_SearchIsError = false;
    m_SelectedIdx = -1;
    m_Replays.clear();

    if (m_SearchThread.joinable()) m_SearchThread.join();
    m_SearchThread = std::thread(&TmxBrowserModule::SearchWorker, this, m_SiteIdx, m_Query, afterCursor,
        m_FilterGamemode, m_FilterEnvironment, m_FilterRoutes, m_FilterMood, m_FilterDifficulty);
}

void TmxBrowserModule::SearchWorker(int siteIdx, std::string query, long long afterCursor,
    int gamemodeSel, int environmentSel, int routesSel, int moodSel, int difficultySel)
{
    std::vector<SearchResult> results;
    std::string status;
    bool isError = false;
    bool hasMore = false;

    // Index 0 in each filter combo is "Any" (no param sent) - a real selection is comboIndex-1.
    std::string path = "/api/tracks?count=15&fields=" + std::string(kFields);
    if (!query.empty())         path += "&name=" + UrlEncode(query);
    if (afterCursor > 0)        path += "&after=" + std::to_string(afterCursor);
    if (gamemodeSel   > 0)      path += "&primarytype=" + std::to_string(gamemodeSel - 1);
    if (environmentSel > 0)     path += "&environment=" + std::to_string(environmentSel - 1);
    if (routesSel     > 0)      path += "&routes=" + std::to_string(routesSel - 1);
    if (moodSel       > 0)      path += "&mood=" + std::to_string(moodSel - 1);
    if (difficultySel > 0)      path += "&difficulty=" + std::to_string(difficultySel - 1);

    std::vector<unsigned char> data;
    std::string err;
    if (!HttpGet(kSiteHosts[siteIdx], path, data, err))
    {
        status = "Search failed: " + err;
        isError = true;
    }
    else
    {
        std::string json(data.begin(), data.end());
        hasMore = json.find("\"More\":true") != std::string::npos;

        if (JsonResultsEmpty(json))
        {
            status = "No maps found.";
        }
        else
        {
            for (auto& obj : JsonSplitResultsObjects(json))
            {
                long long trackId = 0;
                std::string name;
                if (!JsonFindInt(obj, "TrackId", trackId)) continue;
                JsonFindString(obj, "TrackName", name);

                SearchResult r;
                r.siteIdx   = siteIdx;
                r.trackId   = trackId;
                r.trackName = name.empty() ? ("Track " + std::to_string(trackId)) : name;
                JsonFindInt(obj, "PrimaryType", r.gamemode);
                JsonFindInt(obj, "Difficulty", r.difficulty);
                results.push_back(r);
            }
            status = query.empty()
                ? (std::to_string(results.size()) + " recently uploaded map" + (results.size() == 1 ? "" : "s") + ".")
                : (std::to_string(results.size()) + " map" + (results.size() == 1 ? "" : "s") + " found.");
        }
    }

    std::lock_guard<std::mutex> lock(m_SearchResultMutex);
    m_PendingResults       = std::move(results);
    m_PendingSearchStatus  = status;
    m_PendingSearchIsError = isError;
    m_PendingHasMorePages  = hasMore;
    m_SearchResultPending  = true;
}

void TmxBrowserModule::ApplyPendingSearchIfAny()
{
    if (!m_SearchResultPending) return;

    std::vector<SearchResult> results;
    std::string status;
    bool isError = false;
    bool hasMore = false;
    {
        std::lock_guard<std::mutex> lock(m_SearchResultMutex);
        results  = std::move(m_PendingResults);
        status   = m_PendingSearchStatus;
        isError  = m_PendingSearchIsError;
        hasMore  = m_PendingHasMorePages;
        m_SearchResultPending = false;
    }

    m_SearchInFlight = false;
    ReleaseAllThumbTextures();
    m_Results       = std::move(results);
    m_SearchStatus  = status;
    m_SearchIsError = isError;
    m_HasMorePages  = hasMore;

    for (auto& r : m_Results) QueueThumbnail(r.siteIdx, r.trackId);
}

// =============================================================================
// Replay list ("View Replays" - top 10, TMX's own default sort is best-first - Position:0 is
// the fastest).
// =============================================================================
void TmxBrowserModule::StartReplaySearch(int siteIdx, long long trackId)
{
    if (m_ReplayInFlight) return;

    m_ReplayInFlight    = true;
    m_ReplaysStatus      = "Loading replays...";
    m_ReplaysIsError     = false;
    m_ReplaysForTrackId  = trackId;
    m_ReplaysSiteIdx     = siteIdx;
    m_Replays.clear();

    if (m_ReplayThread.joinable()) m_ReplayThread.join();
    m_ReplayThread = std::thread(&TmxBrowserModule::ReplayWorker, this, siteIdx, trackId);
}

void TmxBrowserModule::ReplayWorker(int siteIdx, long long trackId)
{
    std::vector<ReplayResult> results;
    std::string status;
    bool isError = false;

    std::string path = "/api/replays?trackid=" + std::to_string(trackId) + "&count=10&fields=ReplayId,Score,Position";
    std::vector<unsigned char> data;
    std::string err;
    if (!HttpGet(kSiteHosts[siteIdx], path, data, err))
    {
        // /api/replays 404s (rather than an empty array) when a track has no replays - treat
        // that specific case as "none" rather than an error.
        if (err.find("404") != std::string::npos)
            status = "No replays for this map yet.";
        else
        {
            status = "Failed to load replays: " + err;
            isError = true;
        }
    }
    else
    {
        std::string json(data.begin(), data.end());
        for (auto& obj : JsonSplitResultsObjects(json))
        {
            ReplayResult r;
            long long pos = 0;
            if (!JsonFindInt(obj, "ReplayId", r.replayId)) continue;
            // Some replays have "Score":null/"Position":null (not part of TMX's ranked
            // leaderboard) - skip those rather than showing a fake "0:00.00" for them.
            if (!JsonFindInt(obj, "Score", r.scoreMs)) continue;
            if (!JsonFindInt(obj, "Position", pos)) continue;
            r.position = (int)pos;
            results.push_back(r);
        }
        std::sort(results.begin(), results.end(), [](const ReplayResult& a, const ReplayResult& b) { return a.position < b.position; });
        status = std::to_string(results.size()) + " replay" + (results.size() == 1 ? "" : "s") + ".";
    }

    std::lock_guard<std::mutex> lock(m_ReplayResultMutex);
    m_PendingReplays        = std::move(results);
    m_PendingReplaysStatus  = status;
    m_PendingReplaysIsError = isError;
    m_ReplayResultPending   = true;
}

void TmxBrowserModule::ApplyPendingReplaysIfAny()
{
    if (!m_ReplayResultPending) return;

    std::vector<ReplayResult> results;
    std::string status;
    bool isError = false;
    {
        std::lock_guard<std::mutex> lock(m_ReplayResultMutex);
        results = std::move(m_PendingReplays);
        status  = m_PendingReplaysStatus;
        isError = m_PendingReplaysIsError;
        m_ReplayResultPending = false;
    }

    m_ReplayInFlight = false;
    m_Replays        = std::move(results);
    m_ReplaysStatus  = status;
    m_ReplaysIsError = isError;
}

// =============================================================================
// Play Map - download the .Challenge.Gbx then hand it to the already-running game, same
// /useexedir /singleinst /file= mechanism Randomizer.cpp's OpenDownloadedTrackInGame uses.
// =============================================================================
void TmxBrowserModule::StartPlayMap(int siteIdx, long long trackId, std::string trackName)
{
    if (m_PlayInFlight) return;

    m_PlayInFlight = true;
    m_PlayStatus   = "Downloading '" + trackName + "'...";
    m_PlayIsError  = false;

    if (m_PlayThread.joinable()) m_PlayThread.join();
    m_PlayThread = std::thread(&TmxBrowserModule::PlayMapWorker, this, siteIdx, trackId, trackName);
}

void TmxBrowserModule::PlayMapWorker(int siteIdx, long long trackId, std::string trackName)
{
    std::string localPath;
    bool ok = false;
    std::string status;

    std::vector<unsigned char> data;
    std::string err;
    if (!HttpGet(kSiteHosts[siteIdx], "/trackgbx/" + std::to_string(trackId), data, err) || data.empty())
    {
        status = "Failed to download map: " + err;
    }
    else
    {
        Filesystem::path outDir = DownloadDir("Challenges");
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> suffixDist(0, 9999);
        char suffixBuf[8];
        snprintf(suffixBuf, sizeof(suffixBuf), "%04d", suffixDist(rng));

        Filesystem::path outPath = outDir / (SanitizeForFilename(trackName) + " - TMX#" + std::to_string(trackId) + " " + suffixBuf + ".Challenge.Gbx");
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open())
        {
            status = "Could not write map file to disk.";
        }
        else
        {
            out.write((const char*)data.data(), (std::streamsize)data.size());
            out.close();
            localPath = outPath.string();
            ok = true;
            status = "Ready: " + trackName;
        }
    }

    std::lock_guard<std::mutex> lock(m_PlayResultMutex);
    m_PendingPlayLocalPath = localPath;
    m_PendingPlayOk        = ok;
    m_PlayStatus            = status; // reused directly - only this worker writes it while in flight
    m_PlayResultPending     = true;
}

void TmxBrowserModule::ApplyPendingPlayIfAny()
{
    if (!m_PlayResultPending) return;

    std::string localPath;
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_PlayResultMutex);
        localPath = m_PendingPlayLocalPath;
        ok        = m_PendingPlayOk;
        m_PlayResultPending = false;
    }

    m_PlayInFlight = false;
    m_PlayIsError  = !ok;
    if (ok) OpenTrackFileInGame(localPath);
}

void TmxBrowserModule::OpenTrackFileInGame(const std::string& localPath) const
{
    if (localPath.empty()) return;

    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
    {
        Logger->PrintError("[TMX] Could not resolve the running game executable path.");
        return;
    }

    // ShellExecuteW (not the ANSI A variant) since localPath is UTF-8 and can contain non-ASCII
    // characters - TMX-downloaded names get sanitized to plain ASCII, but a locally-scanned ghost
    // (Local Replays) can have any Unicode characters in its filename.
    std::wstring args = L"/useexedir /singleinst /file=\"" + Twinkie->UTF8ToWString(localPath) + L"\"";
    HINSTANCE result = ShellExecuteW(NULL, L"open", exePath, args.c_str(), NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
        Logger->PrintErrorArgs("[TMX] Failed to launch game with map (ShellExecute code {})", (INT_PTR)result);
}

// Downloads one replay to disk - fire-and-forget on its own short-lived thread per click (a
// handful of these at once, from clicking a few "Download" buttons in the replay list, is fine;
// unlike the persistent thumbnail queue there's no risk of a search re-queuing dozens at once).
void TmxBrowserModule::DownloadReplayToDisk(int siteIdx, long long replayId, const std::string& trackName, int rank)
{
    std::thread([this, siteIdx, replayId, trackName, rank]()
    {
        std::vector<unsigned char> data;
        std::string err;
        if (!HttpGet(kSiteHosts[siteIdx], "/recordgbx/" + std::to_string(replayId), data, err) || data.empty())
        {
            Logger->PrintErrorArgs("[TMX] Failed to download replay {}: {}", replayId, err);
            return;
        }

        Filesystem::path outDir = DownloadDir("Replays");
        Filesystem::path outPath = outDir / (SanitizeForFilename(trackName) + " - #" + std::to_string(rank + 1) + " - TMX#" + std::to_string(replayId) + ".Replay.Gbx");
        std::ofstream out(outPath, std::ios::binary);
        if (!out.is_open())
        {
            Logger->PrintError("[TMX] Could not write replay file to disk.");
            return;
        }
        out.write((const char*)data.data(), (std::streamsize)data.size());
        out.close();
        Logger->PrintInternalArgs("[TMX] Replay saved to '{}'", outPath.string());
    }).detach();
}

// Same launch mechanism as Play Map (/useexedir /singleinst /file=..., a standard TMF file
// association, not a memory poke) - a replay just isn't a self-contained thing to open the way a
// map is, so this makes sure the challenge it needs is actually sitting on disk first.
void TmxBrowserModule::ViewReplayInGame(int siteIdx, long long trackId, long long replayId, const std::string& trackName, int rank)
{
    std::thread([this, siteIdx, trackId, replayId, trackName, rank]()
    {
        static std::mt19937 rng{ std::random_device{}() };
        std::uniform_int_distribution<int> suffixDist(0, 9999);
        char suffixBuf[8];

        std::vector<unsigned char> mapData;
        std::string err;
        if (!HttpGet(kSiteHosts[siteIdx], "/trackgbx/" + std::to_string(trackId), mapData, err) || mapData.empty())
        {
            Logger->PrintErrorArgs("[TMX] Failed to download map for replay {}: {}", replayId, err);
            return;
        }

        snprintf(suffixBuf, sizeof(suffixBuf), "%04d", suffixDist(rng));
        Filesystem::path mapPath = Filesystem::path(DownloadDir("Challenges"))
            / (SanitizeForFilename(trackName) + " - TMX#" + std::to_string(trackId) + " " + suffixBuf + ".Challenge.Gbx");
        std::ofstream mapOut(mapPath, std::ios::binary);
        if (!mapOut.is_open())
        {
            Logger->PrintError("[TMX] Could not write map file to disk.");
            return;
        }
        mapOut.write((const char*)mapData.data(), (std::streamsize)mapData.size());
        mapOut.close();

        std::vector<unsigned char> replayData;
        if (!HttpGet(kSiteHosts[siteIdx], "/recordgbx/" + std::to_string(replayId), replayData, err) || replayData.empty())
        {
            Logger->PrintErrorArgs("[TMX] Failed to download replay {}: {}", replayId, err);
            return;
        }

        Filesystem::path replayPath = Filesystem::path(DownloadDir("Replays"))
            / (SanitizeForFilename(trackName) + " - #" + std::to_string(rank + 1) + " - TMX#" + std::to_string(replayId) + ".Replay.Gbx");
        std::ofstream replayOut(replayPath, std::ios::binary);
        if (!replayOut.is_open())
        {
            Logger->PrintError("[TMX] Could not write replay file to disk.");
            return;
        }
        replayOut.write((const char*)replayData.data(), (std::streamsize)replayData.size());
        replayOut.close();

        Logger->PrintInternalArgs("[TMX] Opening replay '{}' (map downloaded to '{}')", replayPath.string(), mapPath.string());
        OpenTrackFileInGame(replayPath.string());
    }).detach();
}

bool TmxBrowserModule::IsLoggedIn() const
{
    return Twinkie->GetPayingAccountType() != TM::AccountType::Disconnected;
}

// =============================================================================
// Thumbnails - one persistent worker thread pulls (siteIdx, trackId) requests off a queue,
// downloads https://{site}/trackshow/{id}/image/1 (the same image used for the track page's
// og:image preview) to a small on-disk cache, and hands the path back for the main thread to
// decode into a D3D9 texture (WIC/D3D calls have to happen on the owning thread).
// =============================================================================
void TmxBrowserModule::QueueThumbnail(int siteIdx, long long trackId)
{
    {
        std::lock_guard<std::mutex> lock(m_ThumbQueueMutex);
        m_ThumbQueue.push_back({ siteIdx, trackId });
    }

    if (!m_ThumbThreadRunning)
    {
        m_ThumbThreadRunning = true;
        if (m_ThumbThread.joinable()) m_ThumbThread.join();
        m_ThumbThread = std::thread(&TmxBrowserModule::ThumbWorkerLoop, this);
    }
}

void TmxBrowserModule::ThumbWorkerLoop()
{
    for (;;)
    {
        ThumbRequest req;
        bool have = false;
        {
            std::lock_guard<std::mutex> lock(m_ThumbQueueMutex);
            if (!m_ThumbQueue.empty())
            {
                req = m_ThumbQueue.front();
                m_ThumbQueue.erase(m_ThumbQueue.begin());
                have = true;
            }
        }

        if (!have)
        {
            m_ThumbThreadRunning = false;
            return; // queue drained - QueueThumbnail restarts this loop if more come in later
        }

        std::string cacheDir = DownloadDir("Thumbs");
        Filesystem::path cachePath = Filesystem::path(cacheDir) / (kSiteNames[req.siteIdx] + std::string("_") + std::to_string(req.trackId) + ".jpg");

        bool ok = Filesystem::exists(cachePath);
        if (!ok)
        {
            std::vector<unsigned char> data;
            std::string err;
            if (HttpGet(kSiteHosts[req.siteIdx], "/trackshow/" + std::to_string(req.trackId) + "/image/1", data, err) && !data.empty())
            {
                std::ofstream out(cachePath, std::ios::binary);
                if (out.is_open())
                {
                    out.write((const char*)data.data(), (std::streamsize)data.size());
                    out.close();
                    ok = true;
                }
            }
        }

        std::lock_guard<std::mutex> lock(m_ThumbReadyMutex);
        m_ThumbReadyQueue.push_back({ req.siteIdx, req.trackId, ok ? cachePath.string() : "" });
    }
}

static IDirect3DTexture9* MakeTexFromBGRA(IDirect3DDevice9* dev, const BYTE* bgra, UINT w, UINT h)
{
    IDirect3DTexture9* tex = nullptr;
    if (FAILED(dev->CreateTexture(w, h, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &tex, nullptr)))
        return nullptr;
    D3DLOCKED_RECT lr;
    if (FAILED(tex->LockRect(0, &lr, nullptr, 0))) { tex->Release(); return nullptr; }
    const BYTE* src = bgra;
    BYTE*       dst = (BYTE*)lr.pBits;
    for (UINT y = 0; y < h; y++) { memcpy(dst, src, w * 4); src += w * 4; dst += lr.Pitch; }
    tex->UnlockRect(0);
    return tex;
}

// Decodes a cached JPEG file into a D3D9 texture via WIC (the same OS-native decoder Colors.cpp
// already uses for its PNG - WIC's JPEG codec is a standard, always-present Windows component,
// not something added for this). Must run on the main/render thread - texture creation isn't
// thread-safe against the D3D9 device.
static IDirect3DTexture9* DecodeJpegToTexture(IDirect3DDevice9* dev, const std::string& path)
{
    if (!dev || path.empty()) return nullptr;

    HRESULT hr = CoInitialize(nullptr);
    bool comOwn = (hr == S_OK);

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_IWICImagingFactory, (void**)&factory)))
    {
        if (comOwn) CoUninitialize();
        return nullptr;
    }

    int wn = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wn, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wn);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    IDirect3DTexture9* result = nullptr;
    IWICBitmapDecoder* decoder = nullptr;
    if (SUCCEEDED(factory->CreateDecoderFromFilename(wpath.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &decoder)))
    {
        IWICBitmapFrameDecode* frame = nullptr;
        UINT fw = 0, fh = 0;
        if (SUCCEEDED(decoder->GetFrame(0, &frame)) && SUCCEEDED(frame->GetSize(&fw, &fh)) && fw > 0 && fh > 0)
        {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(factory->CreateFormatConverter(&conv)))
            {
                conv->Initialize(frame, GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
                std::vector<BYTE> px((size_t)fw * fh * 4);
                conv->CopyPixels(nullptr, fw * 4, (UINT)px.size(), px.data());
                conv->Release();
                result = MakeTexFromBGRA(dev, px.data(), fw, fh);
            }
            frame->Release();
        }
        decoder->Release();
    }

    factory->Release();
    if (comOwn) CoUninitialize();
    return result;
}

void TmxBrowserModule::ApplyPendingThumbsIfAny()
{
    std::vector<ThumbReady> ready;
    {
        std::lock_guard<std::mutex> lock(m_ThumbReadyMutex);
        if (m_ThumbReadyQueue.empty()) return;
        ready = std::move(m_ThumbReadyQueue);
        m_ThumbReadyQueue.clear();
    }

    IDirect3DDevice9* dev = Twinkie->GetD3DDevice();
    for (auto& r : ready)
    {
        for (auto& res : m_Results)
        {
            if (res.trackId != r.trackId) continue;
            res.thumbRequested = true;
            if (r.localPath.empty())
            {
                res.thumbFailed = true;
            }
            else
            {
                IDirect3DTexture9* tex = DecodeJpegToTexture(dev, r.localPath);
                if (tex) res.thumbTex = tex;
                else     res.thumbFailed = true;
            }
            break;
        }
    }
}

void TmxBrowserModule::ReleaseAllThumbTextures()
{
    for (auto& r : m_Results)
    {
        if (r.thumbTex)
        {
            ((IDirect3DTexture9*)r.thumbTex)->Release();
            r.thumbTex = nullptr;
        }
    }
}

// =============================================================================
// UI
// =============================================================================
void TmxBrowserModule::Render()
{
    if (!Enabled) return;

    ApplyPendingSearchIfAny();
    ApplyPendingReplaysIfAny();
    ApplyPendingPlayIfAny();
    ApplyPendingThumbsIfAny();

    using namespace ImGui;

    SetNextWindowSize(ImVec2(480.f, 560.f), ImGuiCond_FirstUseEver);
    if (!Begin(ICON_FK_SEARCH " TMX Browser", &Enabled))
    {
        End();
        return;
    }

    bool tmnf = m_SiteIdx == 0;
    if (tmnf) PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 1.f, 1.f));
    if (Button(kSiteNames[0], ImVec2(80.f, 0.f)))
    {
        m_SiteIdx = 0;
        m_FilterEnvironment = 0; // TMNF is Stadium-only in practice - the filter's hidden for it, so make sure a lingering selection can't silently apply
    }
    if (tmnf) PopStyleColor();
    SameLine();
    bool tmuf = m_SiteIdx == 1;
    if (tmuf) PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 1.f, 1.f));
    if (Button(kSiteNames[1], ImVec2(80.f, 0.f))) m_SiteIdx = 1;
    if (tmuf) PopStyleColor();

    // Page nav, right-aligned on the same row - TMX's /api/tracks only offers a forward cursor
    // (see m_PageCursors' comment), so Previous re-fetches an earlier page from a remembered
    // cursor rather than the API reversing itself.
    char pageLabel[16];
    snprintf(pageLabel, sizeof(pageLabel), "Page %d", m_CurrentPage + 1);
    float pagerWidth = 30.f + 30.f + CalcTextSize(pageLabel).x + GetStyle().ItemSpacing.x * 2.f;
    SameLine(GetWindowWidth() - pagerWidth - GetStyle().WindowPadding.x);

    BeginDisabled(m_SearchInFlight || m_CurrentPage == 0);
    if (Button(ICON_FK_CHEVRON_LEFT "##tmxPrevPage"))
        StartSearchPage(-1);
    EndDisabled();
    SameLine();
    TextDisabled("%s", pageLabel);
    SameLine();
    BeginDisabled(m_SearchInFlight || !m_HasMorePages);
    if (Button(ICON_FK_CHEVRON_RIGHT "##tmxNextPage"))
        StartSearchPage(1);
    EndDisabled();

    SetNextItemWidth(-90.f);
    bool enterPressed = InputTextWithHint("##tmxQuery", "Map name... (blank = recently uploaded)", m_QueryBuf, sizeof(m_QueryBuf), ImGuiInputTextFlags_EnterReturnsTrue);
    m_Query = m_QueryBuf;
    SameLine();
    if (Button(ICON_FK_SEARCH " Search", ImVec2(80.f, 0.f)) || enterPressed)
        StartSearch();

    if (Checkbox(ICON_FK_STAR " Favorites", &m_ShowFavoritesOnly))
    {
        if (m_ShowFavoritesOnly) ShowFavoritesInResults();
        else                     StartSearch();
    }
    if (IsItemHovered())
        SetTooltip("Show only your starred maps instead of searching.");

    if (!m_ShowFavoritesOnly && CollapsingHeader(ICON_FK_FILTER " Filters"))
    {
        SetNextItemWidth(130.f);
        Combo("Type", &m_FilterGamemode, kAnyGamemodeNames, IM_ARRAYSIZE(kAnyGamemodeNames));
        if (m_SiteIdx == 1) // TMNF is Stadium-only in practice - an environment filter is meaningless there
        {
            SameLine();
            SetNextItemWidth(130.f);
            Combo("Environment", &m_FilterEnvironment, kAnyEnvironmentNames, IM_ARRAYSIZE(kAnyEnvironmentNames));
        }

        SetNextItemWidth(130.f);
        Combo("Routes", &m_FilterRoutes, kAnyRoutesNames, IM_ARRAYSIZE(kAnyRoutesNames));
        SameLine();
        SetNextItemWidth(130.f);
        Combo("Mood", &m_FilterMood, kAnyMoodNames, IM_ARRAYSIZE(kAnyMoodNames));

        SetNextItemWidth(130.f);
        Combo("Difficulty", &m_FilterDifficulty, kAnyDifficultyNames, IM_ARRAYSIZE(kAnyDifficultyNames));
        SameLine();
        if (Button("Apply"))
            StartSearch();
        TextDisabled("These only apply to Search, not Favorites.");
    }

    if (!m_SearchStatus.empty())
        TextColored(m_SearchIsError ? ImVec4(1.f, 0.3f, 0.3f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f), "%s", m_SearchStatus.c_str());

    if (!IsLoggedIn())
        TextColored({ 1.f, 0.6f, 0.2f, 1.f }, "Log into your TMNF/TMUF account first to Play Map or View replays.");

    Separator();

    // Both list panels below size themselves off however much of the window is actually left,
    // instead of a hardcoded height - so resizing the window (or the extra replay panel showing
    // up under the results) doesn't leave dead space or clip content.
    // Nothing shows at all once a "View Replays" lookup comes back with zero replays - only
    // while still loading (so there's some feedback that a click actually did something) or once
    // there are real replays to show.
    bool  showReplays   = m_SelectedIdx >= 0 && m_SelectedIdx < (int)m_Results.size()
                       && (m_ReplayInFlight || !m_Replays.empty());
    float lineH         = GetTextLineHeightWithSpacing();
    float availTotal    = GetContentRegionAvail().y;
    float statusReserve = m_PlayStatus.empty() ? 0.f : lineH;
    float resultsHeight = showReplays ? (availTotal - statusReserve) * 0.55f : (availTotal - statusReserve);
    if (resultsHeight < 80.f) resultsHeight = 80.f;

    BeginChild("##tmxResults", ImVec2(0.f, resultsHeight), true);
    for (size_t i = 0; i < m_Results.size(); i++)
    {
        SearchResult& r = m_Results[i];
        PushID((int)i);

        bool isFav = IsFavorite(r.siteIdx, r.trackId);
        if (Button(isFav ? ICON_FK_STAR : ICON_FK_STAR_O))
            ToggleFavorite(r.siteIdx, r.trackId, r.trackName);
        if (IsItemHovered())
            SetTooltip(isFav ? "Unfavorite" : "Favorite - makes this map easy to find again later.");
        SameLine();

        ImVec2 thumbSize(64.f, 48.f);
        if (r.thumbTex)
        {
            Image((ImTextureID)r.thumbTex, thumbSize);
            if (IsItemHovered())
            {
                BeginTooltip();
                Image((ImTextureID)r.thumbTex, ImVec2(thumbSize.x * m_HoverPreviewScale, thumbSize.y * m_HoverPreviewScale));
                EndTooltip();
            }
        }
        else
        {
            Dummy(thumbSize);
            GetWindowDrawList()->AddRect(GetItemRectMin(), GetItemRectMax(), IM_COL32(90, 90, 90, 255));
        }

        SameLine();
        BeginGroup();
        TextWrapped("%s [%s]", r.trackName.c_str(), kSiteNames[r.siteIdx]);
        if (r.gamemode >= 0 && r.difficulty >= 0)
        {
            const char* gamemodeName   = (r.gamemode   < 6) ? kGamemodeNames[r.gamemode]     : "?";
            const char* difficultyName = (r.difficulty < 4) ? kDifficultyNames[r.difficulty] : "?";
            TextDisabled("%s - %s", gamemodeName, difficultyName);
        }

        BeginDisabled(m_PlayInFlight || !IsLoggedIn());
        if (Button(ICON_FK_PLAY " Play Map"))
            StartPlayMap(r.siteIdx, r.trackId, r.trackName);
        EndDisabled();
        SameLine();
        BeginDisabled(m_ReplayInFlight);
        if (Button(ICON_FK_LIST " View Replays"))
        {
            m_SelectedIdx = (int)i;
            StartReplaySearch(r.siteIdx, r.trackId);
        }
        EndDisabled();
        SameLine();
        if (Button(ICON_FK_EXTERNAL_LINK " TMX"))
            ShellExecuteA(NULL, "open", TmxUrl(r.siteIdx, r.trackId).c_str(), NULL, NULL, SW_SHOWNORMAL);
        EndGroup();

        Separator();
        PopID();
    }
    EndChild();

    if (m_FavoritesNeedRefresh)
    {
        m_FavoritesNeedRefresh = false;
        ShowFavoritesInResults();
    }

    if (!m_PlayStatus.empty())
        TextColored(m_PlayIsError ? ImVec4(1.f, 0.3f, 0.3f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f), "%s", m_PlayStatus.c_str());

    if (showReplays)
    {
        Separator();
        Text("Replays for: %s", m_Results[m_SelectedIdx].trackName.c_str());
        SameLine(GetWindowWidth() - 40.f);
        if (SmallButton(ICON_FK_TIMES "##tmxCloseReplays"))
            m_SelectedIdx = -1;
        if (IsItemHovered())
            SetTooltip("Close");

        if (m_SelectedIdx >= 0 && !m_ReplaysStatus.empty())
            TextColored(m_ReplaysIsError ? ImVec4(1.f, 0.3f, 0.3f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f), "%s", m_ReplaysStatus.c_str());

        if (m_SelectedIdx >= 0)
        {
            float replaysHeight = GetContentRegionAvail().y - lineH * 2.f; // reserve for the two footer lines below
            if (replaysHeight < 60.f) replaysHeight = 60.f;
            BeginChild("##tmxReplays", ImVec2(0.f, replaysHeight), true);
            for (size_t i = 0; i < m_Replays.size(); i++)
            {
                ReplayResult& rep = m_Replays[i];
                PushID((int)i);
                Text("#%d", rep.position + 1);
                SameLine(50.f);
                TextColored(rep.position == 0 ? ImVec4(1.f, 0.85f, 0.1f, 1.f) : ImVec4(0.9f, 0.9f, 0.9f, 1.f), "%s", FormatTimeHMSMs(rep.scoreMs).c_str());
                SameLine(160.f);
                BeginDisabled(!IsLoggedIn());
                if (SmallButton(ICON_FK_PLAY " View"))
                    ViewReplayInGame(m_ReplaysSiteIdx, m_ReplaysForTrackId, rep.replayId, m_Results[m_SelectedIdx].trackName, rep.position);
                EndDisabled();
                if (IsItemHovered())
                    SetTooltip("Downloads this replay (and the map it needs) and opens it in-game.");
                SameLine();
                if (SmallButton(ICON_FK_DOWNLOAD " Download"))
                    DownloadReplayToDisk(m_ReplaysSiteIdx, rep.replayId, m_Results[m_SelectedIdx].trackName, rep.position);
                PopID();
            }
            EndChild();
            TextDisabled("Downloaded replays go to Documents\\TrackMania\\Tracks\\Replays\\TMX -");
            TextDisabled("\"View\" also grabs the map so the replay actually loads correctly.");
        }
    }

    End();
}

void TmxBrowserModule::RenderSettings()
{
    using namespace ImGui;

    TextWrapped("TrackMania Exchange browser - search, favorite, and download maps/replays "
                "without leaving the game.");
    Separator();

    SetNextItemWidth(200.f);
    SliderFloat("Thumbnail hover preview size", &m_HoverPreviewScale, 1.f, 10.f, "%.1fx");
    if (IsItemHovered())
        SetTooltip("How much bigger the popup preview is than the small thumbnail\nshown in the results list, when you hover over it.");
}

void TmxBrowserModule::SettingsInit(SettingMgr& Settings)
{
    // Deliberately not persisting Enable - the browser window should always start closed rather
    // than popping back open just because it happened to be open when the game last closed.
    Settings["TmxBrowser"]["SiteIdx"].GetAsInt(&m_SiteIdx);
    if (m_SiteIdx != 0 && m_SiteIdx != 1) m_SiteIdx = 1;
    Settings["TmxBrowser"]["HoverPreviewScale"].GetAsFloat(&m_HoverPreviewScale);
    if (m_HoverPreviewScale < 1.f || m_HoverPreviewScale > 10.f) m_HoverPreviewScale = 4.f;
    LoadFavorites();
}

void TmxBrowserModule::SettingsSave(SettingMgr& Settings)
{
    Settings["TmxBrowser"]["SiteIdx"].Set(m_SiteIdx);
    Settings["TmxBrowser"]["HoverPreviewScale"].Set(m_HoverPreviewScale);
}

TmxBrowserModule::~TmxBrowserModule()
{
    m_ThumbThreadRunning = false;
    if (m_SearchThread.joinable()) m_SearchThread.join();
    if (m_ReplayThread.joinable())  m_ReplayThread.join();
    if (m_PlayThread.joinable())    m_PlayThread.join();
    if (m_ThumbThread.joinable())   m_ThumbThread.join();
    ReleaseAllThumbTextures();
}
