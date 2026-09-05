#pragma once

#include "../../IModule.h"
#include <vector>
#include <string>

// Browses ghosts already sitting in Documents\TrackMania\Tracks\Replays - for finding and opening
// a ghost you already have, rather than searching TMX. Lets you navigate into subfolders (bounded
// at the Replays root), or type a name to search recursively across all of them.
class LocalReplaysModule : public IModule
{
public:
    LocalReplaysModule(TwinkTrackmania& Twinkie, TwinkLogs& Logger, const bool* UiRenderEnabled)
    {
        this->UiRenderEnabled = UiRenderEnabled;
        this->Twinkie         = &Twinkie;
        this->Logger          = &Logger;
        this->Name            = "LocalReplays";
        this->FancyName       = "Local Replays";
        this->Category        = "Track Tools";
    }

    virtual void Render()         override;
    virtual void RenderAnyways()  override {}
    virtual void RenderMenuItem() override;
    virtual void RenderSettings() override {}
    virtual void SettingsInit(SettingMgr& Settings) override;
    virtual void SettingsSave(SettingMgr& Settings)  override;
    virtual bool HasSettings()    override { return false; }

private:
    struct Entry
    {
        std::string        name;             // stripped display name (folder or file)
        std::string        path;             // full path
        bool                isDirectory  = false;
        unsigned long long  sizeBytes    = 0; // 0 for directories
        long long           lastWriteEpochSeconds = 0; // 0 for directories
    };

    struct FavoriteEntry
    {
        std::string path;
        std::string name; // stripped display name, cached in case the file's been moved/renamed since
    };

    bool        m_Loaded = false;
    char        m_SearchBuf[128] = "";

    // Folder navigation - m_CurrentDir starts at (and can never go above) RootDir(). Contents are
    // scanned non-recursively; typing a search instead ignores the current folder and searches the
    // whole tree from the root down.
    std::string m_CurrentDir;
    std::vector<Entry> m_Entries;

    // Sort mode applied to whichever list is currently on screen (folder view, search results, or
    // favorites) - 0 = alphabetical by name, 1 = most recently modified first.
    int m_SortMode = 0;
    void SortEntries(std::vector<Entry>& list) const;

    // Favorites - persisted to disk so a pinned ghost stays easy to find regardless of which
    // folder it's actually in. Same \x1f-delimited line format TmxBrowser's own Favorites use.
    bool m_ShowFavoritesOnly = false;
    std::vector<FavoriteEntry> m_Favorites;
    std::string FavoritesFilePath() const;
    void LoadFavorites();
    void SaveFavorites();
    bool IsFavorite(const std::string& path) const;
    void ToggleFavorite(const std::string& path, const std::string& name);

    std::string RootDir() const;
    void LoadCurrentDir();       // non-recursive listing of m_CurrentDir
    void NavigateInto(const std::string& folderPath);
    void NavigateUp();
    void SearchAllReplays(std::vector<Entry>& outResults, const std::string& query) const; // recursive, files only

    void OpenTrackFileInGame(const std::string& localPath) const;
    void CopyFileToClipboard(const std::string& localPath) const;
};
