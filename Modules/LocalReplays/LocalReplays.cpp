#include "LocalReplays.h"
#include "../../SettingMgr/SettingMgr.h"
#include "../../TwinkTrackmania/TwinkTrackmania.h"
#include "../../TwinkLogs.h"
#include "../../GlyphTable/IconsForkAwesome.h"
#include "../../imgui-dx9/imgui.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <cwctype>
#include <chrono>
#include <ctime>
#include <Windows.h>
#include <shellapi.h>
#pragma comment(lib, "shell32.lib")

namespace Filesystem = std::filesystem;

// TrackMania map/replay names carry inline formatting codes ($f00 for a hex color, $w/$o/$i/$t/
// $s/$g/$z etc for styles, $$ for a literal '$') - stripping them out lets a search for "A01-Race"
// match a filename like "$fffA01-Race - #1 - TMX#12345.Replay.Gbx" without the user typing any of
// that. $$ collapses to a single literal $, so a search for just "$" naturally lands on a filename
// that has an escaped dollar sign in it, rather than one with a color code.
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

static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](char a, char b) { return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); });
    return it != haystack.end();
}

static std::string FormatFileSize(unsigned long long bytes)
{
    const char* units[] = { "B", "KB", "MB", "GB" };
    double size = (double)bytes;
    int unitIdx = 0;
    while (size >= 1024.0 && unitIdx < 3)
    {
        size /= 1024.0;
        unitIdx++;
    }
    char buf[32];
    snprintf(buf, sizeof(buf), unitIdx == 0 ? "%.0f %s" : "%.1f %s", size, units[unitIdx]);
    return buf;
}

static long long FileTimeToEpochSeconds(Filesystem::file_time_type ftime)
{
    auto sctp = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
    return (long long)std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
}

static std::string FormatEpochSeconds(long long epochSeconds)
{
    std::time_t t = (std::time_t)epochSeconds;
    std::tm tmBuf{};
    localtime_s(&tmBuf, &t);
    char buf[64];
    strftime(buf, sizeof(buf), "%b %d, %Y %I:%M %p", &tmBuf);
    return buf;
}

std::string LocalReplaysModule::RootDir() const
{
    // u8path() parses GetDocumentsFolder()'s bytes as UTF-8 rather than assuming the system ANSI
    // codepage (which a plain Filesystem::path(std::string) construction would do) - matters for
    // any account whose Windows username/Documents path itself has non-ASCII characters.
    Filesystem::path dir = Filesystem::u8path(GetDocumentsFolder()) / "TrackMania" / "Tracks" / "Replays";
    return Twinkie->WStringToUTF8(dir.wstring());
}

// =============================================================================
// Folder navigation
// =============================================================================
void LocalReplaysModule::LoadCurrentDir()
{
    m_Entries.clear();

    std::error_code ec;
    Filesystem::path dir = Filesystem::u8path(m_CurrentDir);
    if (!Filesystem::exists(dir, ec)) return;

    for (auto it = Filesystem::directory_iterator(dir, Filesystem::directory_options::skip_permission_denied, ec);
        it != Filesystem::directory_iterator(); it.increment(ec))
    {
        if (ec) break;

        std::error_code ec2;
        bool isDir = it->is_directory(ec2);
        if (!isDir)
        {
            // Stay in wstring for the extension check and only convert to UTF-8 once we know
            // we're keeping the entry - .string() would silently mangle any non-ASCII character,
            // and ImGui expects UTF-8, not the system ANSI codepage .string() gives.
            std::wstring extW = it->path().extension().wstring();
            for (auto& c : extW) c = (wchar_t)std::towlower(c);
            if (extW != L".gbx") continue;
        }

        Entry entry;
        entry.isDirectory = isDir;
        entry.name        = Twinkie->WStringToUTF8(it->path().filename().wstring());
        entry.path        = Twinkie->WStringToUTF8(it->path().wstring());
        entry.sizeBytes   = isDir ? 0ULL : it->file_size(ec2);
        entry.lastWriteEpochSeconds = isDir ? 0LL : FileTimeToEpochSeconds(it->last_write_time(ec2));
        m_Entries.push_back(entry);
    }

    SortEntries(m_Entries);
}

void LocalReplaysModule::NavigateInto(const std::string& folderPath)
{
    m_CurrentDir = folderPath;
    LoadCurrentDir();
}

void LocalReplaysModule::NavigateUp()
{
    if (m_CurrentDir == RootDir()) return; // can't go above the Replays folder itself

    Filesystem::path parent = Filesystem::u8path(m_CurrentDir).parent_path();
    m_CurrentDir = Twinkie->WStringToUTF8(parent.wstring());
    LoadCurrentDir();
}

void LocalReplaysModule::SearchAllReplays(std::vector<Entry>& outResults, const std::string& query) const
{
    outResults.clear();

    std::error_code ec;
    Filesystem::path dir = Filesystem::u8path(RootDir());
    if (!Filesystem::exists(dir, ec)) return;

    for (auto it = Filesystem::recursive_directory_iterator(dir, Filesystem::directory_options::skip_permission_denied, ec);
        it != Filesystem::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;

        std::wstring extW = it->path().extension().wstring();
        for (auto& c : extW) c = (wchar_t)std::towlower(c);
        if (extW != L".gbx") continue;

        Entry entry;
        entry.isDirectory = false;
        entry.name        = Twinkie->WStringToUTF8(it->path().filename().wstring());
        if (!query.empty() && !ContainsCaseInsensitive(StripTmFormatting(entry.name), query))
            continue;

        entry.path = Twinkie->WStringToUTF8(it->path().wstring());
        std::error_code ec2;
        entry.sizeBytes = it->file_size(ec2);
        entry.lastWriteEpochSeconds = FileTimeToEpochSeconds(it->last_write_time(ec2));
        outResults.push_back(entry);
    }

    SortEntries(outResults);
}

void LocalReplaysModule::SortEntries(std::vector<Entry>& list) const
{
    std::sort(list.begin(), list.end(), [this](const Entry& a, const Entry& b)
    {
        if (a.isDirectory != b.isDirectory) return a.isDirectory > b.isDirectory; // folders first
        if (m_SortMode == 1) return a.lastWriteEpochSeconds > b.lastWriteEpochSeconds; // newest first
        return a.name < b.name;
    });
}

// =============================================================================
// Favorites
// =============================================================================
std::string LocalReplaysModule::FavoritesFilePath() const
{
    Filesystem::path dir = Filesystem::u8path(GetDocumentsFolder()) / "TwinkPlanet";
    std::error_code ec;
    Filesystem::create_directories(dir, ec);
    return Twinkie->WStringToUTF8((dir / "LocalReplaysFavorites.ini").wstring());
}

void LocalReplaysModule::LoadFavorites()
{
    m_Favorites.clear();
    std::ifstream in(Filesystem::u8path(FavoritesFilePath()));
    if (!in.is_open()) return;

    std::string line;
    while (std::getline(in, line))
    {
        size_t sep = line.find('\x1f');
        if (sep == std::string::npos) continue;

        FavoriteEntry f;
        f.path = line.substr(0, sep);
        f.name = line.substr(sep + 1);
        if (!f.name.empty() && f.name.back() == '\r') f.name.pop_back();
        m_Favorites.push_back(f);
    }
}

void LocalReplaysModule::SaveFavorites()
{
    std::ofstream out(Filesystem::u8path(FavoritesFilePath()), std::ios::trunc);
    if (!out.is_open()) return;
    for (auto& f : m_Favorites)
        out << f.path << '\x1f' << f.name << "\n";
}

bool LocalReplaysModule::IsFavorite(const std::string& path) const
{
    for (auto& f : m_Favorites)
        if (f.path == path) return true;
    return false;
}

void LocalReplaysModule::ToggleFavorite(const std::string& path, const std::string& name)
{
    for (auto it = m_Favorites.begin(); it != m_Favorites.end(); ++it)
    {
        if (it->path == path)
        {
            m_Favorites.erase(it);
            SaveFavorites();
            return;
        }
    }

    FavoriteEntry f;
    f.path = path;
    f.name = name;
    m_Favorites.push_back(f);
    SaveFavorites();
}

// =============================================================================
// File actions
// =============================================================================

// Same launch mechanism TmxBrowser's Play Map/View Replay use (/useexedir /singleinst /file=..., a
// standard TMF file association) - ShellExecuteW rather than the ANSI A variant since localPath is
// UTF-8 and a locally-scanned ghost can have any Unicode characters in its filename.
void LocalReplaysModule::OpenTrackFileInGame(const std::string& localPath) const
{
    if (localPath.empty()) return;

    wchar_t exePath[MAX_PATH] = {};
    if (!GetModuleFileNameW(NULL, exePath, MAX_PATH))
    {
        Logger->PrintError("[Local Replays] Could not resolve the running game executable path.");
        return;
    }

    std::wstring args = L"/useexedir /singleinst /file=\"" + Twinkie->UTF8ToWString(localPath) + L"\"";
    HINSTANCE result = ShellExecuteW(NULL, L"open", exePath, args.c_str(), NULL, SW_SHOWNORMAL);
    if ((INT_PTR)result <= 32)
        Logger->PrintErrorArgs("[Local Replays] Failed to launch game with map (ShellExecute code {})", (INT_PTR)result);
}

// Puts the actual file on the Windows clipboard as a CF_HDROP - pasting (Ctrl+V) in Explorer
// copies the file there, the same as copying it from a File Explorer window would.
void LocalReplaysModule::CopyFileToClipboard(const std::string& localPath) const
{
    if (localPath.empty()) return;

    std::wstring wpath = Twinkie->UTF8ToWString(localPath);

    size_t pathChars = wpath.size() + 1; // + single null terminator
    size_t totalSize = sizeof(DROPFILES) + (pathChars + 1) * sizeof(wchar_t); // +1 wchar for the list's final double-null

    HGLOBAL hGlobal = GlobalAlloc(GHND, totalSize);
    if (!hGlobal)
    {
        Logger->PrintError("[Local Replays] Could not allocate clipboard memory for file copy.");
        return;
    }

    DROPFILES* df = (DROPFILES*)GlobalLock(hGlobal);
    if (!df)
    {
        GlobalFree(hGlobal);
        Logger->PrintError("[Local Replays] Could not lock clipboard memory for file copy.");
        return;
    }

    df->pFiles = sizeof(DROPFILES);
    df->fWide  = TRUE;
    wchar_t* dest = (wchar_t*)((BYTE*)df + sizeof(DROPFILES));
    memcpy(dest, wpath.c_str(), wpath.size() * sizeof(wchar_t));
    dest[wpath.size()] = L'\0';
    dest[wpath.size() + 1] = L'\0';
    GlobalUnlock(hGlobal);

    if (!OpenClipboard(NULL))
    {
        GlobalFree(hGlobal);
        Logger->PrintError("[Local Replays] Could not open the clipboard.");
        return;
    }

    EmptyClipboard();
    if (!SetClipboardData(CF_HDROP, hGlobal))
    {
        GlobalFree(hGlobal);
        Logger->PrintError("[Local Replays] Could not set clipboard data.");
    }
    CloseClipboard();
}

// =============================================================================
// UI
// =============================================================================
void LocalReplaysModule::RenderMenuItem()
{
    using namespace ImGui;
    if (MenuItem(ICON_FK_FOLDER_OPEN " Local Replays", "", Enabled))
        Enabled = !Enabled;
}

void LocalReplaysModule::SettingsInit(SettingMgr& Settings)
{
    LoadFavorites();
}

void LocalReplaysModule::SettingsSave(SettingMgr& Settings)
{
}

void LocalReplaysModule::Render()
{
    if (!Enabled) return;
    if (!m_Loaded)
    {
        m_CurrentDir = RootDir();
        LoadCurrentDir();
        m_Loaded = true;
    }

    using namespace ImGui;

    SetNextWindowSize(ImVec2(820.f, 560.f), ImGuiCond_FirstUseEver);
    if (!Begin(ICON_FK_FOLDER_OPEN " Local Replays", &Enabled))
    {
        End();
        return;
    }

    std::string root = RootDir();
    bool atRoot = (m_CurrentDir == root);

    BeginDisabled(atRoot || m_ShowFavoritesOnly);
    if (Button(ICON_FK_CHEVRON_LEFT "##localReplaysUp"))
        NavigateUp();
    EndDisabled();
    if (IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        SetTooltip(atRoot ? "Already at the Replays folder." : "Go up one folder.");
    SameLine();

    std::string relative = (m_CurrentDir.size() > root.size()) ? m_CurrentDir.substr(root.size()) : "";
    TextDisabled("Replays%s", relative.c_str());

    SameLine();
    if (Checkbox(ICON_FK_STAR " Favorites", &m_ShowFavoritesOnly)) {}
    if (IsItemHovered())
        SetTooltip("Show only pinned ghosts instead of browsing folders.");

    // Reserve exactly as much room as the Refresh button actually needs (icon+label width, plus
    // ImGui's frame padding on both sides) instead of a guessed fixed number - a hardcoded
    // reservation clipped the button off the right edge once the window wasn't wide enough.
    float refreshWidth = CalcTextSize(ICON_FK_REFRESH " Refresh").x + GetStyle().FramePadding.x * 2.f;
    SetNextItemWidth(-(refreshWidth + GetStyle().ItemSpacing.x));
    InputTextWithHint("##localReplaysSearch", "Map/file name... (searches every folder)", m_SearchBuf, sizeof(m_SearchBuf));
    SameLine();
    if (Button(ICON_FK_REFRESH " Refresh"))
        LoadCurrentDir();

    TextDisabled("Sort:");
    SameLine();
    bool sortByName = (m_SortMode == 0);
    if (sortByName) PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 1.f, 1.f));
    if (Button("Name")) m_SortMode = 0;
    if (sortByName) PopStyleColor();
    SameLine();
    bool sortByNewest = (m_SortMode == 1);
    if (sortByNewest) PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.5f, 1.f, 1.f));
    if (Button("Last Updated")) m_SortMode = 1;
    if (sortByNewest) PopStyleColor();

    Separator();

    std::string query = StripTmFormatting(m_SearchBuf);
    bool searching = !query.empty() && !m_ShowFavoritesOnly;

    std::vector<Entry> searchResults;
    std::vector<Entry> favoriteResults;
    const std::vector<Entry>* displayList = &m_Entries;

    if (m_ShowFavoritesOnly)
    {
        for (auto& fav : m_Favorites)
        {
            if (!query.empty() && !ContainsCaseInsensitive(StripTmFormatting(fav.name), query))
                continue;

            Entry e;
            e.isDirectory = false;
            e.path = fav.path;
            e.name = fav.name;
            std::error_code ec;
            Filesystem::path favPath = Filesystem::u8path(fav.path);
            e.sizeBytes = Filesystem::file_size(favPath, ec);
            e.lastWriteEpochSeconds = FileTimeToEpochSeconds(Filesystem::last_write_time(favPath, ec));
            favoriteResults.push_back(e);
        }
        SortEntries(favoriteResults);
        displayList = &favoriteResults;
    }
    else if (searching)
    {
        SearchAllReplays(searchResults, query);
        displayList = &searchResults;
    }
    else
    {
        SortEntries(m_Entries);
    }

    unsigned long long totalBytes = 0;
    int fileCount = 0, folderCount = 0;
    for (auto& e : *displayList)
    {
        if (e.isDirectory) folderCount++;
        else { fileCount++; totalBytes += e.sizeBytes; }
    }

    BeginChild("##localReplaysList", ImVec2(0.f, -GetTextLineHeightWithSpacing()), true);
    for (auto& entry : *displayList)
    {
        PushID(entry.path.c_str());

        if (entry.isDirectory)
        {
            if (Selectable((std::string(ICON_FK_FOLDER) + " " + StripTmFormatting(entry.name)).c_str()))
                NavigateInto(entry.path);
        }
        else
        {
            bool isFav = IsFavorite(entry.path);
            if (Button(isFav ? ICON_FK_STAR : ICON_FK_STAR_O))
                ToggleFavorite(entry.path, StripTmFormatting(entry.name));
            if (IsItemHovered())
                SetTooltip(isFav ? "Unpin" : "Pin - keeps this easy to find under Favorites.");
            SameLine();
            BeginGroup();
            TextWrapped("%s", StripTmFormatting(entry.name).c_str());
            TextDisabled("%s - %s", FormatFileSize(entry.sizeBytes).c_str(), FormatEpochSeconds(entry.lastWriteEpochSeconds).c_str());
            if (SmallButton(ICON_FK_PLAY " Open in game"))
                OpenTrackFileInGame(entry.path);
            if (IsItemHovered())
                SetTooltip("Opens this ghost directly - the map it needs must already be installed locally.");
            SameLine();
            if (SmallButton(ICON_FK_FILES_O " Copy file"))
                CopyFileToClipboard(entry.path);
            if (IsItemHovered())
                SetTooltip("Copies this file - paste (Ctrl+V) into any folder in Explorer.");
            EndGroup();
        }

        Separator();
        PopID();
    }
    if (displayList->empty())
    {
        if (m_ShowFavoritesOnly) TextDisabled("No favorites pinned yet.");
        else if (searching)      TextDisabled("No replays match \"%s\".", m_SearchBuf);
        else                     TextDisabled("This folder is empty.");
    }
    EndChild();

    TextDisabled("%d folder%s, %d replay%s, %s total",
        folderCount, folderCount == 1 ? "" : "s",
        fileCount, fileCount == 1 ? "" : "s",
        FormatFileSize(totalBytes).c_str());

    End();
}
