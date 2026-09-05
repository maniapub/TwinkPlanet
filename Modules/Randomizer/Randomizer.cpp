#include "Randomizer.h"
#include <filesystem>
#include <fstream>
#include <random>
#include <cctype>
#include <cstring>
#include <algorithm>
#include <ctime>
#include <cstdlib>
#include <Windows.h>
#include <winhttp.h>
#include <shellapi.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")

namespace Filesystem = std::filesystem;

static const char* kSiteNames[2] = { "TMNF", "TMUF" };
static const char* kSiteHosts[2] = { "tmnf.exchange", "tmuf.exchange" };
static const char* kGoalNames[5] = { "Author", "Gold", "Silver", "Bronze", "Finished" };
// Shared with RenderMedalTimes' own per-medal coloring, so "Goal: Gold - 0:11.51" and the medal
// row below it always agree on what "gold" looks like. Finished has no single target time, so it
// gets a neutral color rather than one of the medal ones.
static const ImVec4 kGoalColors[5] =
{
    { 0.2f, 0.85f, 0.25f, 1.f }, // Author
    { 1.0f, 0.85f, 0.1f, 1.f },  // Gold
    { 0.75f, 0.75f, 0.8f, 1.f }, // Silver
    { 0.8f, 0.55f, 0.25f, 1.f }, // Bronze
    { 0.8f, 0.8f, 0.8f, 1.f },   // Finished
};

static const char* kFields = "TrackId,TrackName,AuthorTime,GoldTarget,SilverTarget,BronzeTarget,Difficulty,PrimaryType,Environment,Routes,Mood";

// Enum orderings taken directly from BigBang1112's randomizer-tmf repo (EDifficulty.cs,
// EPrimaryType.cs, EEnvironment.cs, ERoutes.cs, EMood.cs) - factual name-to-value mappings,
// not creative content, used here to build compatible filters against the same TMX fields.
// No "Any" entry here - these back multi-select toggle buttons, where selecting none of them
// means "any of these" (same convention the real Randomizer TMF app's own filter UI uses).
static const char* kDifficultyNames[4]  = { "Beginner", "Intermediate", "Expert", "Lunatic" };
static const char* kGamemodeNames[6]    = { "Race", "Puzzle", "Platform", "Stunts", "Shortcut", "Laps" };
static const char* kEnvironmentNames[7] = { "Snow", "Desert", "Rally", "Island", "Coast", "Bay", "Stadium" };
static const char* kRoutesNames[3]      = { "Single", "Multi", "Symmetric" };
static const char* kMoodNames[4]        = { "Sunrise", "Day", "Sunset", "Night" };

// Empty (or all-false) selection means "any of these".
static bool AnySelected(const std::vector<bool>& sel)
{
    for (bool b : sel) if (b) return true;
    return false;
}
static bool PassesMultiFilter(const std::vector<bool>& sel, long long value)
{
    if (!AnySelected(sel)) return true;
    return value >= 0 && value < (long long)sel.size() && sel[(size_t)value];
}

// Packs a multi-select bool vector into a bitmask int for settings persistence (max 7 options
// across every filter here, well within a 32-bit int).
static int PackSel(const std::vector<bool>& sel)
{
    int mask = 0;
    for (size_t i = 0; i < sel.size(); i++) if (sel[i]) mask |= (1 << i);
    return mask;
}
static void UnpackSel(int mask, std::vector<bool>& sel)
{
    for (size_t i = 0; i < sel.size(); i++) sel[i] = (mask & (1 << i)) != 0;
}

// =============================================================================
// Tiny JSON field extractors. The response shape is fully predictable since we
// control the `fields=` list in every request, so a full JSON parser isn't
// needed - just pulling named int/string values out by key.
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
    return json.find("\"Results\":[]") != std::string::npos;
}

// Splits the "Results":[{...},{...},...] array into individual object substrings (by brace
// depth) so each one can be run through JsonFindInt/JsonFindString independently - those only
// ever find the *first* match in whatever string they're given, so a multi-result batch (as
// opposed to the single-object response id= queries returned) needs splitting first.
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

// Percent-encodes a string for use in a URL query parameter.
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

static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty() || haystack.size() < needle.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) { return tolower(a) == tolower(b); });
    return it != haystack.end();
}

// Whole-word substring match: the needle must be flanked by non-alphanumeric characters (or
// the start/end of the string) at every occurrence checked, so a query for "ilu" won't match
// "failure" (flanked by letters on both sides there) but will match "blah ILU blah" or "ILU!".
// Used when a "map name contains" search is wrapped in quotes.
static bool ContainsWholeWordCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty() || haystack.size() < needle.size()) return false;

    size_t searchFrom = 0;
    while (searchFrom <= haystack.size() - needle.size())
    {
        auto it = std::search(haystack.begin() + searchFrom, haystack.end(), needle.begin(), needle.end(),
            [](unsigned char a, unsigned char b) { return tolower(a) == tolower(b); });
        if (it == haystack.end()) return false;

        size_t idx = (size_t)(it - haystack.begin());
        bool leftOk  = (idx == 0) || !isalnum((unsigned char)haystack[idx - 1]);
        bool rightOk = (idx + needle.size() >= haystack.size()) || !isalnum((unsigned char)haystack[idx + needle.size()]);
        if (leftOk && rightOk) return true;

        searchFrom = idx + 1;
    }
    return false;
}

// A "map name contains" query wrapped in double quotes (e.g. "ILU") is treated as a whole-word
// match instead of a plain substring - returns the unquoted term and whether quoting was found.
static std::string UnwrapQuotedTerm(const std::string& raw, bool& outQuoted)
{
    outQuoted = raw.size() >= 2 && raw.front() == '"' && raw.back() == '"';
    return outQuoted ? raw.substr(1, raw.size() - 2) : raw;
}

// TrackMania map names carry inline formatting codes ($f00 for a hex color, $w/$o/$i/$t/$s/$g/$z
// etc for styles, $$ for a literal '$') that show up as raw junk if compared or displayed as-is.
// Same approach as TwinkDiscordRP.cpp's own copy of this (kept file-local, same convention).
static std::string StripTmFormatting(const std::string& s)
{
    std::string out;
    out.reserve(s.size());

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] == '$')
        {
            if (i + 1 < s.size() && s[i + 1] == '$')
            {
                out.push_back('$');
                ++i;
                continue;
            }

            // Once '$' is followed by a hex digit, it swallows that digit PLUS up to 2 more
            // arbitrary (non-'$') characters as a single color code - they don't also need to be
            // hex. "$04t" is one 4-character unit ($,0,4,t) that vanishes entirely, not a 2-digit
            // color ("$04") leaving literal "t" behind. Matches AroPix/ForeverRPC's own formatting
            // regex (\$(?:...|[0-9a-f][^$]{0,2})).
            if (i + 1 < s.size() && std::isxdigit((unsigned char)s[i + 1]))
            {
                size_t consumed = 1; // the hex digit itself
                while (consumed < 3 && i + 1 + consumed < s.size() && s[i + 1 + consumed] != '$')
                    consumed++;
                i += consumed;
                continue;
            }

            if (i + 1 < s.size())
            {
                ++i;
                continue;
            }
        }

        out.push_back(s[i]);
    }

    return out;
}

// TMX medal times come back in milliseconds - format as [H:]MM:SS.xx (centiseconds,
// matching real TM display convention), omitting the hour segment entirely when 0.
static std::string FormatTimeHMSMs(long long ms)
{
    if (ms < 0) ms = 0;
    long long h  = ms / 3600000; ms %= 3600000;
    long long m  = ms / 60000;   ms %= 60000;
    long long s  = ms / 1000;    ms %= 1000;
    long long cs = ms / 10; // centiseconds (.xx, not .xxx)

    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld.%02lld", h, m, s, cs);
    else
        snprintf(buf, sizeof(buf), "%lld:%02lld.%02lld", m, s, cs);
    return buf;
}

// Formats a whole number of seconds as [H:]MM:SS, no fractional part - used for the
// session/total playtime timers, which don't need sub-second precision.
static std::string FormatDurationHMS(long long totalSeconds)
{
    if (totalSeconds < 0) totalSeconds = 0;
    long long h = totalSeconds / 3600; totalSeconds %= 3600;
    long long m = totalSeconds / 60;   totalSeconds %= 60;
    long long s = totalSeconds;

    char buf[32];
    if (h > 0)
        snprintf(buf, sizeof(buf), "%lld:%02lld:%02lld", h, m, s);
    else
        snprintf(buf, sizeof(buf), "%lld:%02lld", m, s);
    return buf;
}

// Strips characters Windows doesn't allow in filenames (map names can contain
// almost anything, including quotes/colons from in-game text formatting).
static std::string SanitizeForFilename(const std::string& name)
{
    static const std::string kInvalid = "\\/:*?\"<>|";
    std::string out;
    out.reserve(name.size());
    for (char c : name)
    {
        if (kInvalid.find(c) != std::string::npos || (unsigned char)c < 0x20)
            out += ' ';
        else
            out += c;
    }
    // Collapse doubled spaces left behind by stripped characters
    std::string collapsed;
    bool lastWasSpace = false;
    for (char c : out)
    {
        bool isSpace = (c == ' ');
        if (isSpace && lastWasSpace) continue;
        collapsed += c;
        lastWasSpace = isSpace;
    }
    while (!collapsed.empty() && collapsed.front() == ' ') collapsed.erase(collapsed.begin());
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    return collapsed.empty() ? "Track" : collapsed;
}

// =============================================================================
// HTTP GET over HTTPS via WinHTTP
// =============================================================================
bool RandomizerModule::HttpGet(const std::string& host, const std::string& path, std::vector<unsigned char>& outData, std::string& errorMsg)
{
    int wlenHost = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring whost(wlenHost, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, whost.data(), wlenHost);
    if (!whost.empty() && whost.back() == L'\0') whost.pop_back();

    int wlenPath = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlenPath, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlenPath);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    HINTERNET hSess = WinHttpOpen(L"TwinkieRandomizer/1.0",
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

// =============================================================================
// TMX queries
// =============================================================================

// Parses one already-split result object (see JsonSplitResultsObjects) and applies whichever
// filters TMX's search API doesn't support server-side (already-played, min/max author time) -
// the multi-select filters and name search are applied server-side by
// SearchBatchFor before this ever runs, but are cheap enough to double-check here too.
bool RandomizerModule::ParseAndFilterCandidate(const std::string& objJson, const FetchFilters& filters, FetchResult& out)
{
    long long trackId = 0, authorTime = 0, gold = 0, silver = 0, bronze = 0, difficulty = 0;
    long long gamemode = 0, environmentRaw = 0, routes = 0, mood = 0;
    std::string name;
    if (!JsonFindInt(objJson, "TrackId", trackId)) return false;
    JsonFindString(objJson, "TrackName", name);
    JsonFindInt(objJson, "AuthorTime", authorTime);
    JsonFindInt(objJson, "GoldTarget", gold);
    JsonFindInt(objJson, "SilverTarget", silver);
    JsonFindInt(objJson, "BronzeTarget", bronze);
    JsonFindInt(objJson, "Difficulty", difficulty);
    JsonFindInt(objJson, "PrimaryType", gamemode);
    JsonFindInt(objJson, "Environment", environmentRaw);
    JsonFindInt(objJson, "Routes", routes);
    JsonFindInt(objJson, "Mood", mood);

    // TMX's wire value for Environment is 1-indexed (Stadium reports as 7) while our own enum
    // (matching BigBang1112's EEnvironment.cs) is 0-indexed (Stadium = 6).
    long long environment = environmentRaw - 1;

    if (filters.playedIds.find(trackId) != filters.playedIds.end()) return false;
    if (filters.recentIds.find(trackId) != filters.recentIds.end()) return false;

    if (!filters.nameContains.empty())
    {
        bool quoted = false;
        std::string term = UnwrapQuotedTerm(filters.nameContains, quoted);
        bool matches = quoted ? ContainsWholeWordCaseInsensitive(name, term) : ContainsCaseInsensitive(name, term);
        if (!matches) return false;
    }

    if (!PassesMultiFilter(filters.difficultySel,  difficulty))  return false;
    if (!PassesMultiFilter(filters.gamemodeSel,    gamemode))    return false;
    if (!PassesMultiFilter(filters.environmentSel, environment)) return false;
    if (!PassesMultiFilter(filters.routesSel,      routes))      return false;
    if (!PassesMultiFilter(filters.moodSel,        mood))        return false;

    if (filters.minAuthorSec > 0.f && authorTime < (long long)(filters.minAuthorSec * 1000.f)) return false;
    if (filters.maxAuthorSec > 0.f && authorTime > (long long)(filters.maxAuthorSec * 1000.f)) return false;

    out.trackId      = trackId;
    out.trackName    = name.empty() ? ("Track " + std::to_string(trackId)) : name;
    out.authorTime   = authorTime;
    out.goldTarget   = gold;
    out.silverTarget = silver;
    out.bronzeTarget = bronze;
    out.difficulty   = difficulty;
    return true;
}

// Queries TMX with the filters applied server-side where possible (one concrete value per
// multi-select dimension, since the API only accepts a single value per param - randomly
// re-rolled per attempt so all toggled options get a turn across multiple attempts), pulls a
// batch of matching results, shuffles them, and returns the first one that also passes the
// filters TMX can't apply itself (see ParseAndFilterCandidate).
bool RandomizerModule::SearchBatchFor(int siteIdx, const FetchFilters& filters, std::mt19937_64& rng, FetchResult& out)
{
    auto pickOne = [&](const std::vector<bool>& sel) -> int
    {
        std::vector<int> options;
        for (size_t i = 0; i < sel.size(); i++) if (sel[i]) options.push_back((int)i);
        if (options.empty()) return -1;
        std::uniform_int_distribution<int> d(0, (int)options.size() - 1);
        return options[d(rng)];
    };

    int diff = pickOne(filters.difficultySel);
    int mode = pickOne(filters.gamemodeSel);
    int env  = pickOne(filters.environmentSel);
    int rt   = pickOne(filters.routesSel);
    int md   = pickOne(filters.moodSel);

    std::string path = "/api/tracks?count=60&fields=" + std::string(kFields);
    if (diff >= 0) path += "&difficulty=" + std::to_string(diff);
    if (mode >= 0) path += "&primarytype=" + std::to_string(mode);
    if (env  >= 0) path += "&environment=" + std::to_string(env); // param N -> field N+1 (see the note above ParseAndFilterCandidate)
    if (rt   >= 0) path += "&routes=" + std::to_string(rt);
    if (md   >= 0) path += "&mood=" + std::to_string(md);
    if (!filters.nameContains.empty())
    {
        bool quoted = false;
        std::string term = UnwrapQuotedTerm(filters.nameContains, quoted);
        path += "&name=" + UrlEncode(term); // quotes are a client-side whole-word hint, not sent to TMX
    }

    std::vector<unsigned char> data;
    std::string err;
    if (!HttpGet(kSiteHosts[siteIdx], path, data, err)) return false;

    std::string json(data.begin(), data.end());
    if (JsonResultsEmpty(json)) return false;

    auto objs = JsonSplitResultsObjects(json);
    std::shuffle(objs.begin(), objs.end(), rng);

    for (auto& obj : objs)
    {
        FetchResult candidate;
        if (ParseAndFilterCandidate(obj, filters, candidate))
        {
            // One extra request per otherwise-acceptable candidate, so this only costs anything
            // once a track has already cleared every other filter - see TrackNeedsExtension.
            if (TrackNeedsExtension(siteIdx, candidate.trackId)) continue;

            out = candidate;
            out.siteIdx = siteIdx;
            return true;
        }
    }
    return false;
}

// TMX's track page has an "Extensions" row (e.g. "TMUnlimiter 2.0", or a car mod like
// "NewSnowCar") with a tooltip reading "You must install this extension to play this track."
// whenever the map needs something not in the base game - that row is server-rendered and
// present in the plain page HTML even though most of the page is a JS-driven SPA. Missing/failed
// requests are treated as "unknown, let it through" rather than blocking a fetch on a hiccup.
bool RandomizerModule::TrackNeedsExtension(int siteIdx, long long trackId)
{
    std::vector<unsigned char> data;
    std::string err;
    if (!HttpGet(kSiteHosts[siteIdx], "/trackshow/" + std::to_string(trackId), data, err)) return false;

    std::string html(data.begin(), data.end());
    return html.find("must install this extension") != std::string::npos;
}

bool RandomizerModule::DownloadTrackFileFor(int siteIdx, long long trackId, const std::string& trackName, std::string& outLocalPath, std::string& errorMsg)
{
    std::vector<unsigned char> data;
    std::string err;
    std::string path = "/trackgbx/" + std::to_string(trackId);

    if (!HttpGet(kSiteHosts[siteIdx], path, data, err) || data.empty())
    {
        errorMsg = "Failed to download track file: " + err;
        return false;
    }

    std::string docs = GetDocumentsFolder();
    Filesystem::path outDir = Filesystem::path(docs) / "TrackMania" / "Tracks" / "Challenges" / "RMC";
    std::error_code ec;
    Filesystem::create_directories(outDir, ec);

    // "[Map Name] - TMX#[TrackId] [random 4 digits].Challenge.Gbx"
    // TMX's public API doesn't expose an author-name field, so TrackId stands in as the closest
    // traceable identifier. The random suffix stops two downloads of the same map from
    // overwriting each other on disk.
    static std::mt19937 filenameRng{ std::random_device{}() };
    std::uniform_int_distribution<int> suffixDist(0, 9999);
    char suffixBuf[8];
    snprintf(suffixBuf, sizeof(suffixBuf), "%04d", suffixDist(filenameRng));

    std::string fileBaseName = SanitizeForFilename(trackName) + " - TMX#" + std::to_string(trackId) + " " + suffixBuf;
    Filesystem::path outPath = outDir / (fileBaseName + ".Challenge.Gbx");
    std::ofstream out(outPath, std::ios::binary);
    if (!out.is_open())
    {
        errorMsg = "Could not write track file to disk.";
        return false;
    }
    out.write((const char*)data.data(), (std::streamsize)data.size());
    out.close();

    outLocalPath = outPath.string();
    return true;
}

// =============================================================================
// Played-track history, so the same map doesn't get served again in a later session.
// Stored as a plain one-ID-per-line text file (named RMC.ini for discoverability, though it's
// not a real key/value ini - there's nothing here but a flat list) next to the downloaded maps,
// in Documents\TrackMania\Tracks\Challenges\RMC.
// =============================================================================
std::string RandomizerModule::PlayedTrackIdsFilePath() const
{
    std::string docs = GetDocumentsFolder();
    Filesystem::path dir = Filesystem::path(docs) / "TrackMania" / "Tracks" / "Challenges" / "RMC";
    std::error_code ec;
    Filesystem::create_directories(dir, ec);
    return (dir / "RMC.ini").string();
}

void RandomizerModule::LoadPlayedTrackIds()
{
    m_PlayedTrackIds.clear();

    std::ifstream in(PlayedTrackIdsFilePath());
    if (!in.is_open()) return;

    long long id = 0;
    while (in >> id) m_PlayedTrackIds.insert(id);
}

void RandomizerModule::SavePlayedTrackIds()
{
    std::ofstream out(PlayedTrackIdsFilePath(), std::ios::trunc);
    if (!out.is_open()) return;

    for (long long id : m_PlayedTrackIds) out << id << "\n";
}

// =============================================================================
// History log - a separate file from RMC.ini (played-track blacklist) since this tracks richer,
// user-facing data (name/site/when/favorite) rather than just "don't show again" IDs. One line
// per entry, fields separated by \x1f (a control character that will never appear in a real
// track name or file path) - trackName sits between the 4th and 5th delimiter and localPath is
// everything after the 5th, so both can safely contain anything, including a literal "|" or
// comma. A line with only 4 delimiters (saved before localPath existed) is still read fine - it
// just gets an empty localPath, same as any entry that's never actually been downloaded.
// =============================================================================
std::string RandomizerModule::HistoryFilePath() const
{
    std::string docs = GetDocumentsFolder();
    Filesystem::path dir = Filesystem::path(docs) / "TrackMania" / "Tracks" / "Challenges" / "RMC";
    std::error_code ec;
    Filesystem::create_directories(dir, ec);
    return (dir / "RMCHistory.ini").string();
}

void RandomizerModule::LoadHistory()
{
    m_History.clear();

    std::ifstream in(HistoryFilePath());
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        if (line.empty()) continue;

        size_t p1 = line.find('\x1f');
        size_t p2 = p1 == std::string::npos ? std::string::npos : line.find('\x1f', p1 + 1);
        size_t p3 = p2 == std::string::npos ? std::string::npos : line.find('\x1f', p2 + 1);
        size_t p4 = p3 == std::string::npos ? std::string::npos : line.find('\x1f', p3 + 1);
        if (p4 == std::string::npos) continue; // malformed line - skip it

        HistoryEntry entry;
        try
        {
            entry.siteIdx       = std::stoi(line.substr(0, p1));
            entry.trackId       = std::stoll(line.substr(p1 + 1, p2 - p1 - 1));
            entry.favorite      = line.substr(p2 + 1, p3 - p2 - 1) == "1";
            entry.timestampUnix = std::stoll(line.substr(p3 + 1, p4 - p3 - 1));
        }
        catch (...) { continue; }

        size_t p5 = line.find('\x1f', p4 + 1);
        if (p5 == std::string::npos)
        {
            entry.trackName = line.substr(p4 + 1); // older format, saved before localPath existed
        }
        else
        {
            entry.trackName = line.substr(p4 + 1, p5 - p4 - 1);
            entry.localPath = line.substr(p5 + 1);
        }

        m_History.push_back(std::move(entry));
    }
}

void RandomizerModule::SaveHistory()
{
    std::ofstream out(HistoryFilePath(), std::ios::trunc);
    if (!out.is_open()) return;

    for (auto& entry : m_History)
    {
        out << entry.siteIdx << '\x1f' << entry.trackId << '\x1f' << (entry.favorite ? "1" : "0")
            << '\x1f' << entry.timestampUnix << '\x1f' << entry.trackName << '\x1f' << entry.localPath << "\n";
    }
}

// Adds/refreshes a history entry for a track that just became current. Deduplicated by
// (siteIdx, trackId) - a repeat visit just moves the existing entry to the front, updates its
// timestamp, and refreshes localPath (each fetch downloads to a fresh filename - see
// DownloadTrackFileFor's random suffix - so the newest download is the one worth pointing at)
// rather than adding a second row. Favorited entries are always exempt from the count cap below,
// no matter how old - that's the point of favoriting one.
void RandomizerModule::AddHistoryEntry(int siteIdx, long long trackId, const std::string& trackName, const std::string& localPath)
{
    auto it = std::find_if(m_History.begin(), m_History.end(), [&](const HistoryEntry& e)
    {
        return e.siteIdx == siteIdx && e.trackId == trackId;
    });

    long long now = (long long)std::time(nullptr);

    if (it != m_History.end())
    {
        HistoryEntry entry = *it;
        m_History.erase(it);
        entry.timestampUnix = now;
        entry.trackName     = trackName; // in case it changed/was unresolved before
        entry.localPath     = localPath;
        m_History.insert(m_History.begin(), entry);
    }
    else
    {
        HistoryEntry entry;
        entry.siteIdx       = siteIdx;
        entry.trackId       = trackId;
        entry.trackName     = trackName;
        entry.timestampUnix = now;
        entry.localPath     = localPath;
        m_History.insert(m_History.begin(), entry);
    }

    // Prune oldest non-favorited entries beyond the cap - scan from the back since the list is
    // newest-first, so the oldest entries are at the end.
    size_t nonFavCount = 0;
    for (auto& e : m_History) if (!e.favorite) nonFavCount++;
    while (nonFavCount > kMaxHistoryEntries)
    {
        for (auto rit = m_History.rbegin(); rit != m_History.rend(); ++rit)
        {
            if (!rit->favorite)
            {
                m_History.erase(std::next(rit).base());
                nonFavCount--;
                break;
            }
        }
    }

    SaveHistory();
}

// =============================================================================
// Opens the downloaded track in the running game. This uses TmForever's own documented
// command-line switches: launching the game exe again with "/useexedir /singleinst /file=<path>"
// makes the new process detect the already-running instance, hand the file-open request to it
// over the game's own single-instance IPC, and exit - so the currently running game just loads
// the map.
// =============================================================================
void RandomizerModule::OpenDownloadedTrackInGame()
{
    OpenTrackFileInGame(m_LocalPath);
}

// Shared by the "current track" Open-in-game button and the History/Favorites tab's per-row
// Load-map button - takes an explicit path so history entries (which may be for a track other
// than whatever's current right now) can use it too, not just m_LocalPath.
void RandomizerModule::OpenTrackFileInGame(const std::string& localPath) const
{
    if (localPath.empty()) return;

    char exePath[MAX_PATH] = {};
    if (!GetModuleFileNameA(NULL, exePath, MAX_PATH))
    {
        Logger->PrintError("[RMC] Could not resolve the running game executable path.");
        return;
    }

    std::string args = "/useexedir /singleinst /file=\"" + localPath + "\"";
    HINSTANCE result = ShellExecuteA(NULL, "open", exePath, args.c_str(), NULL, SW_SHOWNORMAL);

    if ((INT_PTR)result <= 32)
    {
        Logger->PrintErrorArgs("[RMC] Failed to launch game with map (ShellExecute code {})", (INT_PTR)result);
    }
    else
    {
        Logger->PrintInternalArgs("[RMC] Requested game to open '{}'", localPath);
    }
}

// Runs entirely on a background thread. Only ever touches the "filters" snapshot it was
// handed by value and its own local FetchResult - never the module's live m_Track*/m_Status*
// members - so it's safe to run concurrently with the main thread rendering the UI.
std::string RandomizerModule::GetCurrentTmxUrl() const
{
    if (!m_HasTrack) return "";
    return "https://" + std::string(kSiteHosts[m_TrackSiteIdx]) + "/trackshow/" + std::to_string(m_TrackId);
}

void RandomizerModule::OpenTmxTrackPage()
{
    OpenTmxUrl(m_TrackSiteIdx, m_TrackId);
}

// Shared by the "current track" TMX button and the History/Favorites tab's per-row TMX button.
void RandomizerModule::OpenTmxUrl(int siteIdx, long long trackId) const
{
    std::string url = "https://" + std::string(kSiteHosts[siteIdx]) + "/trackshow/" + std::to_string(trackId);
    ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
}

void RandomizerModule::FetchWorker(FetchFilters filters, FetchTarget target)
{
    FetchResult result;

    // Site is now a multi-select too (empty = both eligible), so each attempt below picks a
    // random eligible site to query rather than being locked to one for the whole fetch.
    std::vector<int> eligibleSites;
    bool anySite = AnySelected(filters.siteSel);
    for (int s = 0; s < 2; s++)
        if (!anySite || (s < (int)filters.siteSel.size() && filters.siteSel[s]))
            eligibleSites.push_back(s);
    if (eligibleSites.empty()) eligibleSites = { 0, 1 };

    static std::mt19937_64 rng{ std::random_device{}() };

    bool found = false;
    // Each attempt queries a server-side filtered batch (see SearchBatchFor) rather than
    // guessing a single ID, so far fewer attempts are needed - this mostly just guards against
    // an exhausted/too-restrictive filter combination.
    for (int attempt = 0; attempt < kMaxFetchAttempts && !found; attempt++)
    {
        m_FetchAttempt = attempt + 1;
        std::uniform_int_distribution<int> siteDist(0, (int)eligibleSites.size() - 1);
        int site = eligibleSites[(size_t)siteDist(rng)];
        if (SearchBatchFor(site, filters, rng, result)) found = true;
    }

    if (!found)
    {
        result.ok = false;
        result.statusMsg = "Could not find a valid track after several attempts - try again.";
        result.statusIsError = true;
    }
    else
    {
        m_FetchDownloading = true;
        std::string localPath, dlErr;
        if (!DownloadTrackFileFor(result.siteIdx, result.trackId, result.trackName, localPath, dlErr))
        {
            result.ok = false;
            result.statusMsg = dlErr;
            result.statusIsError = true;
        }
        else
        {
            result.localPath     = localPath;
            result.ok            = true;
            result.statusMsg     = "Ready: " + result.trackName;
            result.statusIsError = false;
        }
    }

    std::lock_guard<std::mutex> lock(m_ResultMutex);
    m_PendingResult       = result;
    m_PendingResultTarget = target;
    m_ResultPending       = true;
}

void RandomizerModule::StartFetch(FetchTarget target)
{
    if (m_FetchInFlight) return;
    if (!IsLoggedIn()) { m_StatusMsg = "Log into your TMNF/TMUF account first."; m_StatusIsError = true; return; }

    m_FetchInFlight    = true;
    m_InFlightTarget   = target;
    m_FetchAttempt     = 0;
    m_FetchDownloading = false;

    if (target == FetchTarget::Current)
    {
        m_StatusIsError = false;
        m_StatusMsg     = "Fetching...";
    }

    FetchFilters filters;
    filters.siteSel          = m_SiteSel;
    filters.difficultySel    = m_DifficultySel;
    filters.gamemodeSel      = m_GamemodeSel;
    filters.environmentSel   = m_EnvironmentSel;
    filters.routesSel        = m_RoutesSel;
    filters.moodSel          = m_MoodSel;
    filters.minAuthorSec     = m_MinAuthorTimeSec;
    filters.maxAuthorSec     = m_MaxAuthorTimeSec;
    filters.nameContains     = m_NameContains;
    filters.playedIds        = m_PlayedTrackIds;
    filters.recentIds        = std::unordered_set<long long>(m_RecentTrackIds.begin(), m_RecentTrackIds.end());

    if (target == FetchTarget::Next) m_NextTrackFilterSnapshot = BuildFilterSnapshot();

    if (m_FetchThread.joinable()) m_FetchThread.join();
    m_FetchThread = std::thread(&RandomizerModule::FetchWorker, this, filters, target);
}

// A cheap, order-sensitive encoding of every filter that affects what SearchBatchFor asks TMX
// for - used only to tell whether a prefetched "Next" track still matches the current filters
// (see RequestNextTrack), not for anything persisted or user-visible.
std::string RandomizerModule::BuildFilterSnapshot() const
{
    auto packSel = [](const std::vector<bool>& sel)
    {
        std::string s;
        for (bool b : sel) s.push_back(b ? '1' : '0');
        return s;
    };

    return packSel(m_SiteSel) + "|" + packSel(m_DifficultySel) + "|" + packSel(m_GamemodeSel) + "|"
         + packSel(m_EnvironmentSel) + "|" + packSel(m_RoutesSel) + "|" + packSel(m_MoodSel) + "|"
         + m_NameContains + "|"
         + std::to_string((int)(m_MinAuthorTimeSec * 100.f)) + "|"
         + std::to_string((int)(m_MaxAuthorTimeSec * 100.f));
}

// Kicks off fetching the *next* track in the background as soon as a current track exists, so
// by the time the goal is met (or Skip is pressed), it's usually already sitting there ready -
// hiding the TMX lookup + .Gbx download latency behind however long the current track takes.
void RandomizerModule::StartPrefetchNext()
{
    if (m_FetchInFlight || m_NextReady) return;
    if (!IsLoggedIn()) return; // quietly skip - StartFetch would refuse anyway, no need for a status message here
    StartFetch(FetchTarget::Next);
}

void RandomizerModule::ApplyResultToCurrent(const FetchResult& result)
{
    m_HasTrack      = true;
    m_TrackSiteIdx  = result.siteIdx;
    m_TrackId       = result.trackId;
    m_TrackName     = result.trackName;
    m_AuthorTime    = result.authorTime;
    m_GoldTarget    = result.goldTarget;
    m_SilverTarget  = result.silverTarget;
    m_BronzeTarget  = result.bronzeTarget;
    m_Difficulty    = result.difficulty;
    m_LocalPath     = result.localPath;
    m_GoalMet       = false;
    m_LoadConfirmed = false;
    m_TimeoutStarted = false;
    m_TimeoutAccumulatedSec = 0.0;
    m_StatusMsg     = "Ready: " + result.trackName;
    m_StatusIsError = false;
    m_TrackStartTime = std::chrono::steady_clock::now();
    AddHistoryEntry(m_TrackSiteIdx, m_TrackId, m_TrackName, m_LocalPath);

    // Cooldown so the next few fetches can't hand this same track right back - see
    // kRecentCooldownCount's comment for why this is separate from the permanent played-blacklist.
    m_RecentTrackIds.push_back(m_TrackId);
    while (m_RecentTrackIds.size() > kRecentCooldownCount) m_RecentTrackIds.pop_front();

    Logger->PrintInternalArgs("[RMC] Now playing '{}' (id {}) from {}", m_TrackName, m_TrackId, kSiteHosts[m_TrackSiteIdx]);

    if (m_AutoOpen) OpenDownloadedTrackInGame();
}

// Swaps the already-prefetched "next" track into "current" instantly (no network wait), then
// immediately starts prefetching a new "next" behind it.
void RandomizerModule::PromoteNextToCurrent()
{
    ApplyResultToCurrent(m_NextTrack);
    m_NextReady = false;
    m_NextTrack = FetchResult{};
    StartPrefetchNext();
}

// The single entry point for "I want a/the next track now" - used by the initial Fetch button,
// Skip, and goal-completion auto-progress alike. Promotes the prefetched Next if it's ready;
// otherwise either starts a direct fetch (nothing in flight yet) or flags that advancing is
// wanted as soon as whatever's currently fetching lands (see ApplyPendingResultIfAny).
void RandomizerModule::RequestNextTrack()
{
    if (m_NextReady)
    {
        // If a filter changed since this was prefetched, it no longer reflects what's actually
        // being asked for - drop it and fetch fresh instead of serving a stale match.
        if (m_NextTrackFilterSnapshot != BuildFilterSnapshot())
        {
            m_NextReady = false;
            m_NextTrack = FetchResult{};
        }
        else
        {
            PromoteNextToCurrent();
            return;
        }
    }
    if (m_FetchInFlight) { m_AdvancePending = true; return; }
    StartFetch(FetchTarget::Current);
}

// Called every frame from the main thread (RenderAnyways) - the only place that ever writes
// to m_HasTrack/m_TrackName/etc, so there's no need to guard those with a mutex too.
void RandomizerModule::ApplyPendingResultIfAny()
{
    if (!m_ResultPending) return;

    FetchResult result;
    FetchTarget target;
    {
        std::lock_guard<std::mutex> lock(m_ResultMutex);
        result = m_PendingResult;
        target = m_PendingResultTarget;
        m_ResultPending = false;
    }

    m_FetchInFlight    = false;
    m_FetchDownloading = false;

    // Still drain the pending result above even when disabled, so the mutex-guarded slot never
    // gets stuck - but don't act on it. Stop RMC clicked while a fetch was in flight shouldn't
    // have that fetch open a map once it lands after the fact.
    if (!Enabled) return;

    if (target == FetchTarget::Current)
    {
        m_StatusMsg     = result.statusMsg;
        m_StatusIsError = result.statusIsError;
        if (!result.ok) return;
        ApplyResultToCurrent(result);
        StartPrefetchNext();
    }
    else // Next
    {
        if (result.ok)
        {
            m_NextTrack = result;
            m_NextReady = true;
            Logger->PrintInternalArgs("[RMC] Prefetched next track '{}' (id {})", result.trackName, result.trackId);
        }
        else
        {
            Logger->PrintErrorArgs("[RMC] Prefetch of next track failed: {}", result.statusMsg);
        }

        if (m_AdvancePending)
        {
            m_AdvancePending = false;
            RequestNextTrack();
        }
    }
}

RandomizerModule::~RandomizerModule()
{
    if (m_FetchThread.joinable()) m_FetchThread.join();
}

// =============================================================================
// Goal checking - reuses the same edge-triggered "just finished" pattern
// SplitSpeeds already uses (CurrentState == Finished && CurrentState != LastState)
// =============================================================================
long long RandomizerModule::GoalThreshold() const
{
    switch (m_GoalIdx)
    {
        case 0: return m_AuthorTime;
        case 1: return m_GoldTarget;
        case 2: return m_SilverTarget;
        case 3: return m_BronzeTarget;
        default: return -1; // Finished: any completion counts
    }
}

void RandomizerModule::CheckGoalCompletion()
{
    if (!m_HasTrack || m_GoalMet) return;

    TM::RaceState state = Twinkie->GetState();
    bool justFinished = (state == TM::RaceState::Finished and state != m_LastState);
    m_LastState = state;

    if (!justFinished) return;

    long      raceTime  = Twinkie->GetRaceTime();
    long long threshold = GoalThreshold();

    bool met = (m_GoalIdx == 4) ? true : (threshold > 0 and (long long)raceTime <= threshold);
    if (met)
    {
        m_GoalMet = true;
        m_TracksPlayed++;
        m_PlayedTrackIds.insert(m_TrackId);
        SavePlayedTrackIds();
        Logger->PrintInternalArgs("[RMC] Goal met on '{}' (time {} vs threshold {})", m_TrackName, raceTime, threshold);
        if (m_AutoProgress) RequestNextTrack();
    }
}

// If enabled, auto-skips a track that's gone unbeaten too long - only time spent actually racing
// (RaceState::Running) counts toward the budget. Every other moment - the initial load, a
// loading-screen transition into the next map (e.g. "Computing shadows..."), the "3,2,1,GO"
// countdown, and any countdown re-entered later via a respawn/restart - pauses the clock rather
// than letting it run through, so none of that time is ever counted.
void RandomizerModule::CheckTimeout()
{
    if (!m_EnableTimeout || !m_HasTrack || m_GoalMet) return;

    // While the next map is still loading in, GetState()/IsPlaying() can keep reporting stale
    // data left over from whatever was active before (this function is only reached once
    // IsPlaying() is already true) - m_LoadConfirmed only flips once the in-game challenge name
    // actually matches this track (see CheckLoadFailure), so gating on it here means the loading
    // screen itself never gets mistaken for real running time.
    if (!m_LoadConfirmed) { m_TimeoutLastTick = std::chrono::steady_clock::now(); return; }

    auto now = std::chrono::steady_clock::now();

    if (Twinkie->GetState() == TM::RaceState::Running)
    {
        if (m_TimeoutStarted)
            m_TimeoutAccumulatedSec += std::chrono::duration<double>(now - m_TimeoutLastTick).count();
        m_TimeoutStarted  = true;
        m_TimeoutLastTick = now;
    }
    else
    {
        // Not actually racing right now - just keep the tick anchor fresh so re-entering
        // Running later doesn't count the paused gap as elapsed.
        m_TimeoutLastTick = now;
    }

    if (!m_TimeoutStarted || m_TimeoutAccumulatedSec < m_TimeoutSeconds) return;

    Logger->PrintInternalArgs("[RMC] Timed out on '{}' after {:.0f}s of actual driving - skipping.", m_TrackName, m_TimeoutAccumulatedSec);
    SkipTrack();
}

// Some TMX maps need blocks/content the local install doesn't have (env-mixed maps most often) -
// the game just refuses to load them with its own "Could not load challenge!" popup, which TMX's
// metadata can't reliably predict ahead of time (no queryable field for it - see MentionsEnvMix's
// comment on why that heuristic is best-effort only). Rather than guessing, this confirms the
// load actually succeeded (the in-game challenge name starts matching what was fetched) within a
// short window, and treats a track that never shows up as broken - blacklisting it (so it's never
// suggested again) and auto-advancing, same as if it had timed out.
void RandomizerModule::CheckLoadFailure()
{
    if (!m_HasTrack || m_LoadConfirmed || IsBlockingFetch()) return;

    std::string liveName = StripTmFormatting(Twinkie->GetChallengeName());
    std::string wantName = StripTmFormatting(m_TrackName);
    auto trim = [](std::string s)
    {
        while (!s.empty() && isspace((unsigned char)s.front())) s.erase(s.begin());
        while (!s.empty() && isspace((unsigned char)s.back()))  s.pop_back();
        return s;
    };
    liveName = trim(liveName);
    wantName = trim(wantName);

    // Matching the challenge name alone isn't enough: TMF appears to set the challenge's display
    // name from the Gbx's own metadata *before* it actually tries to instantiate the track's
    // blocks - which is the step a "You need these blocks: ... TMUnlimiter required" failure
    // happens at. That left this confirming a load that never actually finished, because the name
    // was already right while the game sat on its own blocking error dialog. Requiring
    // IsPlaying() too (a real vehicle/race object existing, not just a name) means a track stuck
    // behind that dialog is correctly never confirmed, so the kLoadCheckSeconds timeout below
    // still catches it.
    if (!liveName.empty() && !wantName.empty() && ContainsCaseInsensitive(liveName, wantName) && Twinkie->IsPlaying())
    {
        m_LoadConfirmed = true;
        return;
    }

    double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - m_TrackStartTime).count();
    if (elapsed < kLoadCheckSeconds) return;

    Logger->PrintErrorArgs("[RMC] '{}' (id {}) never loaded in-game after {:.0f}s - likely missing blocks/extensions/TMUnlimiter. Blacklisting and skipping.", m_TrackName, m_TrackId, elapsed);
    m_PlayedTrackIds.insert(m_TrackId);
    SavePlayedTrackIds();
    m_StatusMsg     = "Skipped '" + m_TrackName + "' - it didn't load in-game (missing blocks/extensions/mods).";
    m_StatusIsError = true;
    m_LoadConfirmed = true; // stop re-checking this same track while the next one fetches
    RequestNextTrack();
}

void RandomizerModule::SkipTrack()
{
    if (m_HasTrack) m_TracksSkipped++;
    RequestNextTrack();
}

// Called every frame regardless of Enabled state, so m_LastTickTime never falls behind and
// re-enabling the module doesn't count the time it spent disabled as playtime.
void RandomizerModule::TickTimers()
{
    auto now = std::chrono::steady_clock::now();
    double delta = std::chrono::duration<double>(now - m_LastTickTime).count();
    m_LastTickTime = now;

    // Both timers tick on exactly the same condition, so they're always in sync with each other -
    // neither one advances a single frame the other doesn't.
    if (Enabled && m_HasTrack && Twinkie->IsPlaying())
    {
        m_SessionSeconds += delta;
        m_TotalSeconds   += delta;
    }
}

// =============================================================================
// Draws the Author/Gold/Silver/Bronze time row, hiding medals that are redundant given the
// selected goal: since hitting a given medal implies every easier one below it, only the
// medals strictly harder than (or equal to) the goal are worth showing. E.g. goal=Silver hides
// Silver and Bronze (both trivially covered once Silver is met), leaving Author/Gold visible.
// Goal=Finished isn't part of this Author>Gold>Silver>Bronze ranking, so nothing is hidden.
// =============================================================================
void RandomizerModule::RenderMedalTimes()
{
    using namespace ImGui;

    bool shownAny = false;
    auto MaybeShowMedal = [&](int rank, const char* label, ImVec4 color, long long value)
    {
        if (m_GoalIdx <= 3)
        {
            if (rank > m_GoalIdx) return;                     // easier than the goal - always redundant
            // The goal's own tier is redundant too *except* for Author, which has nothing
            // stricter above it to show in its place - hiding it there left the overlay blank.
            if (rank == m_GoalIdx && m_GoalIdx != 0) return;
        }
        if (shownAny) SameLine();
        TextColored(color, "%s %s", label, FormatTimeHMSMs(value).c_str());
        shownAny = true;
    };

    MaybeShowMedal(0, "Author", kGoalColors[0], m_AuthorTime);
    MaybeShowMedal(1, "Gold",   kGoalColors[1], m_GoldTarget);
    MaybeShowMedal(2, "Silver", kGoalColors[2], m_SilverTarget);
    MaybeShowMedal(3, "Bronze", kGoalColors[3], m_BronzeTarget);
}

// =============================================================================
// IModule overrides
// =============================================================================
void RandomizerModule::RenderAnyways()
{
    ApplyPendingResultIfAny();
    TickTimers();

    if (!Enabled) return;

    // Unlike goal-completion/timeout, load-failure has to be checked regardless of IsPlaying() -
    // a track that failed to load never gets the game into a "playing" state in the first place.
    CheckLoadFailure();

    if (Twinkie->IsPlaying())
    {
        CheckGoalCompletion();
        CheckTimeout();
    }

    if (!m_ShowOverlay || (!m_HasTrack && !IsBlockingFetch())) return;

    using namespace ImGui;

    // Skip/TMX need to stay clickable even while F3 is closed (that's the whole point of having
    // them on the always-visible overlay), so this window never gets ImGuiWindowFlags_NoInputs -
    // unlike most of Twinkie's F3-only surfaces, which do get it to avoid stealing clicks from
    // the game while just driving. It's only locked from being dragged while F3 is closed too
    // (same reasoning - don't want a stray drag while just playing), so it can still be
    // repositioned like every other overlay whenever the menu is actually open.
    auto flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoDecoration
               | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing;
    if (!*UiRenderEnabled) flags |= ImGuiWindowFlags_NoMove;

    SetNextWindowBgAlpha(0.8f);
    if (m_WndPosSet) { SetNextWindowPos(m_WndPos, ImGuiCond_Always); m_WndPosSet = false; }
    else { SetNextWindowPos(m_WndPos, ImGuiCond_Appearing); }
    if (!Begin("##RMC", nullptr, flags)) { End(); return; }
    m_WndPos = GetWindowPos();

    Text(ICON_FK_RANDOM " %s", m_TrackName.c_str());
    TextDisabled("Goal:");
    SameLine();
    if (m_GoalIdx == 4) // Finished - no single target time to show
        TextColored(kGoalColors[4], "%s", kGoalNames[4]);
    else
        TextColored(kGoalColors[m_GoalIdx], "%s - %s", kGoalNames[m_GoalIdx], FormatTimeHMSMs(GoalThreshold()).c_str());

    if (!IsLoggedIn())
    {
        Separator();
        TextColored({ 1.f, 0.6f, 0.2f, 1.f }, "Log into your TMNF/TMUF account to fetch maps.");
    }
    else if (IsBlockingFetch())
    {
        Separator();
        if (m_FetchDownloading)
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Downloading map...");
        else if (m_ShowSearchProgress)
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Searching for a track... (%d/%d ids)", m_FetchAttempt.load(), kMaxFetchAttempts);
        else
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Searching for a track...");
    }

    if (!m_HasTrack)
    {
        End();
        return;
    }

    Separator();
    RenderMedalTimes();

    if (m_GoalMet)
        TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "Goal met!");
    else if (m_FetchInFlight && m_InFlightTarget == FetchTarget::Next)
    {
        if (m_FetchDownloading)
            TextDisabled("Prefetching next map...");
        else if (m_ShowSearchProgress)
            TextDisabled("Prefetching next... (%d/%d ids)", m_FetchAttempt.load(), kMaxFetchAttempts);
        else
            TextDisabled("Prefetching next track...");
    }
    else if (m_NextReady)
        TextDisabled("Next track ready.");

    if (m_EnableTimeout && !m_GoalMet)
    {
        if (!m_TimeoutStarted)
            TextDisabled("Auto-skip: waiting for you to start...");
        else
            TextDisabled("Auto-skip in: %s", FormatDurationHMS(TimeoutSecondsLeft()).c_str());
    }

    Separator();
    TextDisabled("Played: %d   Skipped: %d", m_TracksPlayed, m_TracksSkipped);

    if (m_ShowSessionTimer || m_ShowTotalTimer)
    {
        if (m_ShowSessionTimer)
            TextDisabled("Session: %s", FormatDurationHMS(SessionSecondsPlayed()).c_str());
        if (m_ShowSessionTimer && m_ShowTotalTimer) SameLine();
        if (m_ShowTotalTimer)
            TextDisabled("Total: %s", FormatDurationHMS(TotalSecondsPlayed()).c_str());
    }

    BeginDisabled(IsBlockingFetch() || !IsLoggedIn());
    if (Button(ICON_FK_FORWARD " Skip"))
        SkipTrack();
    EndDisabled();
    SameLine();
    if (Button(ICON_FK_EXTERNAL_LINK " TMX"))
        OpenTmxTrackPage();

    End();
}

// Only runs while F3 is open and this module is Enabled (standard IModule::Render()
// semantics) - gives the filters their own always-reachable window instead of being
// buried inside Settings only.
// Minimum total width needed so no toggle-button row's text gets clipped, computed from the
// actual current font metrics (so it adapts to font size/DPI/theme rather than a hardcoded pixel
// guess). AlwaysAutoResize windows size to *last* frame's content, so switching from the History
// tab (narrower content) to Filters for the first time in a while briefly landed on a width too
// narrow for the widest row (e.g. "Stadium" in the 7-column Environment row) before it caught up -
// flooring the window width with this avoids that squished-text frame ever being visible.
static float ComputeFilterGridsMinWidth()
{
    using namespace ImGui;
    float spacing = GetStyle().ItemSpacing.x;
    float padding = GetStyle().FramePadding.x * 2.f;

    auto rowWidth = [&](const char* names[], int count)
    {
        float maxLabel = 0.f;
        for (int i = 0; i < count; i++)
        {
            float w = CalcTextSize(names[i]).x;
            if (w > maxLabel) maxLabel = w;
        }
        return count * (maxLabel + padding) + spacing * (count - 1);
    };

    float minWidth = 0.f;
    float candidates[] =
    {
        rowWidth(kSiteNames, 2),
        rowWidth(kGamemodeNames, 6),
        rowWidth(kEnvironmentNames, 7),
        rowWidth(kRoutesNames, 3),
        rowWidth(kMoodNames, 4),
        rowWidth(kDifficultyNames, 4),
    };
    for (float c : candidates) if (c > minWidth) minWidth = c;
    return minWidth;
}

void RandomizerModule::Render()
{
    if (!m_ShowFilterWindow) return;

    using namespace ImGui;

    SetNextWindowSizeConstraints(ImVec2(ComputeFilterGridsMinWidth(), 0.f), ImVec2(FLT_MAX, FLT_MAX));
    if (!Begin(ICON_FK_RANDOM " RMC", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        End();
        return;
    }

    if (BeginTabBar("##RMCTabs"))
    {
        if (BeginTabItem("Filters"))
        {
            RenderFilterControls();
            EndTabItem();
        }
        if (BeginTabItem("History"))
        {
            RenderHistoryTab();
            EndTabItem();
        }
        EndTabBar();
    }

    End();
}

void RandomizerModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_RANDOM " RMC", "", Enabled))
        Enabled = !Enabled;
}

// Draws one row of equal-width toggle buttons spanning the full available width - matches the
// real Randomizer TMF app's own filter grid look (a tight row of same-size buttons, not
// text-length-sized ones). Selecting none of them means "any" (see PassesMultiFilter).
static void ToggleButtonGrid(const char* names[], std::vector<bool>& sel)
{
    using namespace ImGui;
    int   count    = (int)sel.size();
    float spacing  = GetStyle().ItemSpacing.x;
    float avail    = GetContentRegionAvail().x;
    float btnWidth = (avail - spacing * (count - 1)) / (float)count;

    for (int i = 0; i < count; i++)
    {
        PushID(i);
        bool active = sel[i];
        if (active) PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 1.f, 1.f));
        if (Button(names[i], ImVec2(btnWidth, 0.f))) sel[i] = !sel[i];
        if (active) PopStyleColor();
        PopID();
        if (i + 1 < count) SameLine();
    }
}

// Splits a total-seconds value into separate H/M/S input-box buffers, leaving a component's
// buffer empty (rather than a literal "0") whenever that component is zero - InputTextWithHint's
// hint text then shows a grayed-out "0" there on its own, which is the look being asked for.
static void SecondsToHMSBufs(float totalSeconds, char* hBuf, char* mBuf, char* sBuf, size_t bufSize)
{
    long long total = (long long)totalSeconds;
    int h = (int)(total / 3600);
    int m = (int)((total % 3600) / 60);
    int s = (int)(total % 60);
    hBuf[0] = mBuf[0] = sBuf[0] = '\0';
    if (h > 0) snprintf(hBuf, bufSize, "%d", h);
    if (m > 0) snprintf(mBuf, bufSize, "%d", m);
    if (s > 0) snprintf(sBuf, bufSize, "%d", s);
}

// Three small H/M/S input boxes (each showing a grayed "0" hint when left empty, via
// InputTextWithHint) that combine into a single total-seconds value - so typing only into the
// Seconds box just works, with Hours/Minutes implicitly 0, instead of requiring all three filled.
static void TimeHMSInput(const char* idLabel, const char* trailingLabel, float& totalSeconds,
    char* hBuf, char* mBuf, char* sBuf, size_t bufSize)
{
    using namespace ImGui;
    PushID(idLabel);

    auto DigitsOnly = ImGuiInputTextFlags_CharsDecimal;
    bool changed = false;

    SetNextItemWidth(38.f);
    changed |= InputTextWithHint("##h", "0", hBuf, bufSize, DigitsOnly);
    SameLine(0.f, 3.f); TextDisabled("h");
    SameLine(0.f, 6.f);
    SetNextItemWidth(38.f);
    changed |= InputTextWithHint("##m", "0", mBuf, bufSize, DigitsOnly);
    SameLine(0.f, 3.f); TextDisabled("m");
    SameLine(0.f, 6.f);
    SetNextItemWidth(38.f);
    changed |= InputTextWithHint("##s", "0", sBuf, bufSize, DigitsOnly);
    SameLine(0.f, 3.f); TextDisabled("s");
    SameLine();
    Text("%s", trailingLabel);

    if (changed)
    {
        int h = hBuf[0] ? atoi(hBuf) : 0;
        int m = mBuf[0] ? atoi(mBuf) : 0;
        int s = sBuf[0] ? atoi(sBuf) : 0;
        totalSeconds = (float)(h * 3600 + m * 60 + s);
    }

    PopID();
}

// =============================================================================
// Shared by RenderSettings (inside the Settings window) and Render (a standalone
// window visible whenever F3 is open and this module is Enabled) so the filters
// aren't buried in Settings only.
// =============================================================================
void RandomizerModule::RenderFilterControls()
{
    using namespace ImGui;

    ToggleButtonGrid(kSiteNames, m_SiteSel);
    ToggleButtonGrid(kGamemodeNames, m_GamemodeSel);
    ToggleButtonGrid(kEnvironmentNames, m_EnvironmentSel);
    ToggleButtonGrid(kRoutesNames, m_RoutesSel);
    ToggleButtonGrid(kMoodNames, m_MoodSel);
    ToggleButtonGrid(kDifficultyNames, m_DifficultySel);
    TextDisabled("If a row above has nothing toggled, it simply means \"any of those\".");

    Combo("Goal", &m_GoalIdx, kGoalNames, IM_ARRAYSIZE(kGoalNames));

    if (InputTextWithHint("##nameContains", "Map name contains... (blank = any)", m_NameContainsBuf, sizeof(m_NameContainsBuf)))
        m_NameContains = m_NameContainsBuf;
    if (IsItemHovered())
        SetTooltip("Case-insensitive - ilu matches ILU, vibILUs, etc.\n"
                    "Wrap it in quotes (e.g. \"ILU\") for a whole-word match only -\n"
                    "that won't match something like \"failure\".");

    TimeHMSInput("##minAuthor", "Min author time (0=off)", m_MinAuthorTimeSec,
        m_MinAuthorHBuf, m_MinAuthorMBuf, m_MinAuthorSBuf, sizeof(m_MinAuthorHBuf));
    TimeHMSInput("##maxAuthor", "Max author time (0=off)", m_MaxAuthorTimeSec,
        m_MaxAuthorHBuf, m_MaxAuthorMBuf, m_MaxAuthorSBuf, sizeof(m_MaxAuthorHBuf));

    Checkbox("Auto-skip if goal isn't met in time", &m_EnableTimeout);
    if (IsItemHovered())
        SetTooltip("If the goal isn't met within this many seconds of the track becoming current,\nRMC automatically skips to the next one.");
    if (m_EnableTimeout)
    {
        TimeHMSInput("##timeout", "Timeout", m_TimeoutSeconds,
            m_TimeoutHBuf, m_TimeoutMBuf, m_TimeoutSBuf, sizeof(m_TimeoutHBuf));
    }

    Separator();

    Text("Session: %d played, %d skipped", m_TracksPlayed, m_TracksSkipped);

    if (m_ShowSessionTimer || m_ShowTotalTimer)
    {
        if (m_ShowSessionTimer)
            Text("Session time: %s", FormatDurationHMS(SessionSecondsPlayed()).c_str());
        if (m_ShowSessionTimer && m_ShowTotalTimer) SameLine();
        if (m_ShowTotalTimer)
            Text("Total time: %s", FormatDurationHMS(TotalSecondsPlayed()).c_str());
    }

    if (!IsLoggedIn())
    {
        TextColored({ 1.f, 0.6f, 0.2f, 1.f }, "Log into your TMNF/TMUF account first - RMC can't fetch maps until then.");
    }

    BeginDisabled(IsBlockingFetch() || !IsLoggedIn());
    if (Button(ICON_FK_RANDOM " Fetch random track"))
        RequestNextTrack();
    SameLine();
    BeginDisabled(!m_HasTrack);
    if (Button(ICON_FK_FORWARD " Skip"))
        SkipTrack();
    EndDisabled();
    EndDisabled();
    SameLine();
    if (Button(ICON_FK_REFRESH " Clear Session Timer"))
        m_SessionSeconds = 0.0;
    if (IsItemHovered())
        SetTooltip("Resets the session timer above back to 0 - the total playtime timer is untouched.");

    if (Enabled)
    {
        SameLine();
        if (Button(ICON_FK_STOP " Stop RMC"))
        {
            Enabled          = false;
            m_HasTrack       = false;
            m_NextReady      = false;
            m_NextTrack      = FetchResult{};
            m_AdvancePending = false;
            m_StatusMsg.clear();
            m_StatusIsError  = false;
        }
        if (IsItemHovered())
            SetTooltip("Turns RMC off and clears the current/queued track - a fetch that's\nstill in flight when you click this gets its result thrown away\nrather than opening a map after the fact.");
    }

    if (IsBlockingFetch())
    {
        SameLine();
        if (m_FetchDownloading)
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Downloading map...");
        else if (m_ShowSearchProgress)
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Searching... (%d/%d ids)", m_FetchAttempt.load(), kMaxFetchAttempts);
        else
            TextColored({ 1.f, 0.85f, 0.2f, 1.f }, "Searching...");
    }
    else if (m_FetchInFlight && m_InFlightTarget == FetchTarget::Next)
    {
        SameLine();
        if (m_FetchDownloading)
            TextDisabled("Prefetching next map...");
        else if (m_ShowSearchProgress)
            TextDisabled("Prefetching next... (%d/%d ids)", m_FetchAttempt.load(), kMaxFetchAttempts);
        else
            TextDisabled("Prefetching next track...");
    }
    else if (!m_StatusMsg.empty())
    {
        SameLine();
        TextColored(m_StatusIsError ? ImVec4(1.f, 0.3f, 0.3f, 1.f) : ImVec4(0.6f, 0.6f, 0.6f, 1.f), "%s", m_StatusMsg.c_str());
    }

    if (m_HasTrack)
    {
        Separator();
        Text("Current track: %s", m_TrackName.c_str());
        RenderMedalTimes();
        if (m_GoalMet)
            TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "Goal met!");

        if (Button(ICON_FK_PLAY " Open in game"))
            OpenDownloadedTrackInGame();
        SameLine();
        if (Button(ICON_FK_FOLDER_OPEN " Open containing folder"))
        {
            std::string folder = Filesystem::path(m_LocalPath).parent_path().string();
            ShellExecuteA(NULL, "explore", folder.c_str(), NULL, NULL, SW_SHOWNORMAL);
        }
        SameLine();
        if (Button(ICON_FK_EXTERNAL_LINK " TMX"))
            OpenTmxTrackPage();
        if (IsItemHovered())
            SetTooltip("Opens this track's page on TMX in your browser.");
        TextDisabled("Saved into Documents\\TrackMania\\Tracks\\Challenges\\RMC -");
        TextDisabled("it should show up under that folder in the game's track list.");
    }
}

// =============================================================================
// History tab - search + favorites over the log built by AddHistoryEntry(). The current track
// (if it's in the list at all, which it always is once fetched - see ApplyResultToCurrent) is
// picked out in gold so it's obvious which row you're on right now.
// =============================================================================
void RandomizerModule::RenderHistoryTab()
{
    using namespace ImGui;

    if (InputTextWithHint("##historySearch", "Search history...", m_HistorySearchBuf, sizeof(m_HistorySearchBuf)))
        m_HistorySearch = m_HistorySearchBuf;
    SameLine();
    Checkbox("Favorites only", &m_HistoryFavoritesOnly);

    Separator();

    if (m_History.empty())
    {
        TextDisabled("No history yet - fetch a track to start building one.");
        return;
    }

    BeginChild("##historyList", ImVec2(420.f, 260.f), true);
    bool anyShown = false;
    for (size_t i = 0; i < m_History.size(); i++)
    {
        HistoryEntry& entry = m_History[i];
        if (m_HistoryFavoritesOnly && !entry.favorite) continue;
        if (!m_HistorySearch.empty() && !ContainsCaseInsensitive(entry.trackName, m_HistorySearch)) continue;

        anyShown = true;
        PushID((int)i);

        if (Button(entry.favorite ? ICON_FK_STAR : ICON_FK_STAR_O))
        {
            entry.favorite = !entry.favorite;
            SaveHistory();
        }
        if (IsItemHovered())
            SetTooltip(entry.favorite ? "Unfavorite" : "Favorite - keeps this map listed here no matter how old it gets.");

        SameLine();
        bool isCurrent = m_HasTrack && entry.siteIdx == m_TrackSiteIdx && entry.trackId == m_TrackId;
        if (isCurrent)
            TextColored({ 1.f, 0.85f, 0.1f, 1.f }, "%s", entry.trackName.c_str());
        else
            Text("%s", entry.trackName.c_str());
        SameLine();
        TextDisabled("[%s]", kSiteNames[entry.siteIdx]);

        if (SmallButton(ICON_FK_EXTERNAL_LINK " TMX"))
            OpenTmxUrl(entry.siteIdx, entry.trackId);
        if (IsItemHovered())
            SetTooltip("Opens this track's page on TMX in your browser.");

        SameLine();
        BeginDisabled(entry.localPath.empty());
        if (SmallButton(ICON_FK_PLAY " Load map"))
            OpenTrackFileInGame(entry.localPath);
        EndDisabled();
        if (IsItemHovered())
            SetTooltip(entry.localPath.empty()
                ? "Not downloaded yet - fetch it again to get a local file."
                : "Opens this map in the running game.");

        Separator();
        PopID();
    }
    if (!anyShown)
        TextDisabled(m_HistoryFavoritesOnly ? "No favorites match." : "No history matches that search.");
    EndChild();
}

void RandomizerModule::RenderSettings()
{
    using namespace ImGui;

    TextWrapped("Fetches a real random track from TrackMania Exchange (tmnf.exchange / "
                "tmuf.exchange's public API). Inspired by BigBang1112's Randomizer TMF, "
                "reimplemented from scratch against TMX's REST API rather than porting that "
                "tool's source code.");

    Separator();

    RenderFilterControls();

    Separator();

    Checkbox("Auto-fetch next track on goal completion", &m_AutoProgress);
    Checkbox("Show overlay", &m_ShowOverlay);
    Checkbox("Automatically open track in game after fetching", &m_AutoOpen);
    if (IsItemHovered())
        SetTooltip("Launches TmForever with /singleinst /file=... which hands the map\nto this already-running game instance instead of opening a new window.");
    Checkbox("Show filters window when F3 is open", &m_ShowFilterWindow);
    TextDisabled("(if off, filters are only available here in Settings)");
    Checkbox("Checking IDs progress", &m_ShowSearchProgress);
    if (IsItemHovered())
        SetTooltip("Shows the live \"(X/150 ids)\" attempt counter while searching for a track.\nOff by default to keep the overlay quieter.");

    Separator();
    Checkbox("Show session playtime", &m_ShowSessionTimer);
    if (IsItemHovered())
        SetTooltip("Time spent with RMC turned on since this game launch.");
    SameLine();
    Checkbox("Show total playtime", &m_ShowTotalTimer);
    if (IsItemHovered())
        SetTooltip("Time spent with RMC turned on, added up across all sessions.");

    Text("Session: %s", FormatDurationHMS(SessionSecondsPlayed()).c_str());
    SameLine();
    if (Button(ICON_FK_UNDO " Reset session timer"))
        m_SessionSeconds = 0.0;

    Text("Total: %s", FormatDurationHMS(TotalSecondsPlayed()).c_str());
    SameLine();
    if (Button(ICON_FK_UNDO " Reset total timer"))
        m_TotalSeconds = 0.0;

    Separator();
    Text("Played-track history: %d track%s remembered", (int)m_PlayedTrackIds.size(), m_PlayedTrackIds.size() == 1 ? "" : "s");
    SameLine();
    if (Button(ICON_FK_UNDO " Reset played-track history"))
    {
        m_PlayedTrackIds.clear();
        SavePlayedTrackIds();
    }
    TextDisabled("Saved to Documents\\TrackMania\\Tracks\\Challenges\\RMC\\RMC.ini -");
    TextDisabled("completed tracks are skipped so they don't get served again.");
}

// =============================================================================
// Settings
// =============================================================================
void RandomizerModule::SettingsInit(SettingMgr& Settings)
{
    // Deliberately not persisting Enable - same reasoning as TmxBrowser's own window: it should
    // always start closed rather than popping back open just because it happened to be open when
    // the game last closed.

    int siteMask = PackSel(m_SiteSel);
    Settings["Randomizer"]["SiteMask"].GetAsInt(&siteMask);
    UnpackSel(siteMask, m_SiteSel);

    Settings["Randomizer"]["GoalIdx"].GetAsInt(&m_GoalIdx);
    if (m_GoalIdx < 0 || m_GoalIdx > 4) m_GoalIdx = 1;

    int difficultyMask = 0, gamemodeMask = 0, environmentMask = 0, routesMask = 0, moodMask = 0;
    Settings["Randomizer"]["DifficultyMask"].GetAsInt(&difficultyMask);
    UnpackSel(difficultyMask, m_DifficultySel);
    Settings["Randomizer"]["GamemodeMask"].GetAsInt(&gamemodeMask);
    UnpackSel(gamemodeMask, m_GamemodeSel);
    Settings["Randomizer"]["EnvironmentMask"].GetAsInt(&environmentMask);
    UnpackSel(environmentMask, m_EnvironmentSel);
    Settings["Randomizer"]["RoutesMask"].GetAsInt(&routesMask);
    UnpackSel(routesMask, m_RoutesSel);
    Settings["Randomizer"]["MoodMask"].GetAsInt(&moodMask);
    UnpackSel(moodMask, m_MoodSel);

    Settings["Randomizer"]["MinAuthorTimeSec"].GetAsFloat(&m_MinAuthorTimeSec);
    Settings["Randomizer"]["MaxAuthorTimeSec"].GetAsFloat(&m_MaxAuthorTimeSec);
    SecondsToHMSBufs(m_MinAuthorTimeSec, m_MinAuthorHBuf, m_MinAuthorMBuf, m_MinAuthorSBuf, sizeof(m_MinAuthorHBuf));
    SecondsToHMSBufs(m_MaxAuthorTimeSec, m_MaxAuthorHBuf, m_MaxAuthorMBuf, m_MaxAuthorSBuf, sizeof(m_MaxAuthorHBuf));
    Settings["Randomizer"]["NameContains"].GetAsString(&m_NameContains);
    strncpy_s(m_NameContainsBuf, m_NameContains.c_str(), sizeof(m_NameContainsBuf) - 1);
    Settings["Randomizer"]["AutoProgress"].GetAsBool(&m_AutoProgress);
    Settings["Randomizer"]["ShowOverlay"].GetAsBool(&m_ShowOverlay);
    Settings["Randomizer"]["AutoOpen"].GetAsBool(&m_AutoOpen);
    Settings["Randomizer"]["ShowFilterWindow"].GetAsBool(&m_ShowFilterWindow);
    Settings["Randomizer"]["ShowSearchProgress"].GetAsBool(&m_ShowSearchProgress);
    Settings["Randomizer"]["EnableTimeout"].GetAsBool(&m_EnableTimeout);
    Settings["Randomizer"]["TimeoutSeconds"].GetAsFloat(&m_TimeoutSeconds);
    if (m_TimeoutSeconds <= 0.f) m_TimeoutSeconds = 120.f; // guard against a corrupt/unset value, not a deliberate short one
    SecondsToHMSBufs(m_TimeoutSeconds, m_TimeoutHBuf, m_TimeoutMBuf, m_TimeoutSBuf, sizeof(m_TimeoutHBuf));
    Settings["Randomizer"]["ShowSessionTimer"].GetAsBool(&m_ShowSessionTimer);
    Settings["Randomizer"]["ShowTotalTimer"].GetAsBool(&m_ShowTotalTimer);
    float totalSecondsSaved = 0.f;
    Settings["Randomizer"]["TotalSecondsPlayed"].GetAsFloat(&totalSecondsSaved);
    m_TotalSeconds  = (double)totalSecondsSaved;
    m_SessionSeconds = 0.0;
    m_LastTickTime   = std::chrono::steady_clock::now();
    float px = 0.f, py = 0.f;
    Settings["Randomizer"]["WndPosX"].GetAsFloat(&px);
    Settings["Randomizer"]["WndPosY"].GetAsFloat(&py);
    if (px != 0.f || py != 0.f) { m_WndPos = { px, py }; m_WndPosSet = true; }

    LoadPlayedTrackIds();
    LoadHistory();
}

void RandomizerModule::SettingsSave(SettingMgr& Settings)
{
    Settings["Randomizer"]["SiteMask"].Set(PackSel(m_SiteSel));
    Settings["Randomizer"]["GoalIdx"].Set(m_GoalIdx);
    Settings["Randomizer"]["DifficultyMask"].Set(PackSel(m_DifficultySel));
    Settings["Randomizer"]["GamemodeMask"].Set(PackSel(m_GamemodeSel));
    Settings["Randomizer"]["EnvironmentMask"].Set(PackSel(m_EnvironmentSel));
    Settings["Randomizer"]["RoutesMask"].Set(PackSel(m_RoutesSel));
    Settings["Randomizer"]["MoodMask"].Set(PackSel(m_MoodSel));
    Settings["Randomizer"]["MinAuthorTimeSec"].Set(m_MinAuthorTimeSec);
    Settings["Randomizer"]["MaxAuthorTimeSec"].Set(m_MaxAuthorTimeSec);
    Settings["Randomizer"]["NameContains"].Set(m_NameContains);
    Settings["Randomizer"]["AutoProgress"].Set(m_AutoProgress);
    Settings["Randomizer"]["ShowOverlay"].Set(m_ShowOverlay);
    Settings["Randomizer"]["AutoOpen"].Set(m_AutoOpen);
    Settings["Randomizer"]["ShowFilterWindow"].Set(m_ShowFilterWindow);
    Settings["Randomizer"]["ShowSearchProgress"].Set(m_ShowSearchProgress);
    Settings["Randomizer"]["EnableTimeout"].Set(m_EnableTimeout);
    Settings["Randomizer"]["TimeoutSeconds"].Set(m_TimeoutSeconds);
    Settings["Randomizer"]["ShowSessionTimer"].Set(m_ShowSessionTimer);
    Settings["Randomizer"]["ShowTotalTimer"].Set(m_ShowTotalTimer);
    Settings["Randomizer"]["TotalSecondsPlayed"].Set((float)TotalSecondsPlayed());
    Settings["Randomizer"]["WndPosX"].Set(m_WndPos.x);
    Settings["Randomizer"]["WndPosY"].Set(m_WndPos.y);
}
