#include "TwinkDiscordRP.h"
#include "../Randomizer/Randomizer.h"
#include "../../kiero/minhook/include/MinHook.h"
#include <Windows.h>
#include <winhttp.h>
#include <cctype>
#include <cstring>
#include <ctime>
#include <atomic>
#include <algorithm>
#include <vector>
#pragma comment(lib, "winhttp.lib")

#define LinkOSS(name, url) ImGui::Text(name); ImGui::SameLine(); ImGui::TextLinkOpenURL(url)

// Register your own application at https://discord.com/developers/applications and put its
// numeric client ID here - this can't be a shared/fabricated value, every app needs its own ID
// to show up as itself (name, icon) in a user's Discord profile.
static constexpr long long kDiscordAppId = 1543713952549306528;

// Name of the image uploaded under Rich Presence > Art Assets on the Developer Portal - not the
// app's general icon, which Discord doesn't use for Rich Presence at all.
static constexpr const char* kLargeImageKey = "rpc";

// Built-in presence text, used whenever the matching Settings field is left empty - so a user who
// never opens the customization section gets exactly this, unchanged.
static constexpr const char* kDefaultTemplateMenus    = "In menus";
static constexpr const char* kDefaultTemplateRmc      = "Playing RMC";
static constexpr const char* kDefaultTemplateMap      = "Playing a Map";
static constexpr const char* kDefaultTemplateMapOnServer = "Playing a Map on {server_name}";
static constexpr const char* kDefaultTemplateDetails  = "{map_name}";
static constexpr const char* kDefaultTemplateOfficial = "{map_name} by Nadeo";

// Replaces one "{placeholder}" throughout a template with a value - used to build up
// {map_name}/{server_name}/{mode_name} substitution one placeholder at a time.
static std::string ReplacePlaceholder(const std::string& tmpl, const std::string& placeholder, const std::string& value)
{
    std::string out;
    out.reserve(tmpl.size());
    size_t pos = 0;
    while (pos < tmpl.size())
    {
        if (tmpl.compare(pos, placeholder.size(), placeholder) == 0)
        {
            out += value;
            pos += placeholder.size();
        }
        else
        {
            out += tmpl[pos];
            pos++;
        }
    }
    return out;
}

// =============================================================================
// TrackMania map names carry inline formatting codes ($f00 for a hex color, $w/$o/$i/$t/$s/$g/$z
// etc for styles, $$ for a literal '$') that render fine in-game but would just show up as raw
// junk text in a Discord status. Strips them the same way TMUnlimiter's own map-name display (and
// celyanito/tmuf-dll's discord_presence.cpp, which does the identical thing) does.
// =============================================================================
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

// Unicode bidi-control characters (LRM/RLM, the LRE/RLE/PDF/LRO/RLO/LRI/RLI/FSI/PDI family, and
// ALM) removed from anything sourced from game memory (map/server names) before it reaches
// Discord - a name embedding one of these can otherwise scramble the direction of surrounding text
// in Discord's own UI. Same defensive idea as AroPix/ForeverRPC's force_ltr()/strip_bidi_controls()
// in src/rpc.rs, just implemented as a straight removal (their extra LTR-isolation wrapping is a
// cosmetic nicety on top of this, not needed once the control characters themselves are gone).
static std::string StripBidiControls(const std::string& s)
{
    static const char* kBidiControls[] = {
        "\xE2\x80\x8E", // U+200E LRM
        "\xE2\x80\x8F", // U+200F RLM
        "\xE2\x80\xAA", // U+202A LRE
        "\xE2\x80\xAB", // U+202B RLE
        "\xE2\x80\xAC", // U+202C PDF
        "\xE2\x80\xAD", // U+202D LRO
        "\xE2\x80\xAE", // U+202E RLO
        "\xE2\x81\xA6", // U+2066 LRI
        "\xE2\x81\xA7", // U+2067 RLI
        "\xE2\x81\xA8", // U+2068 FSI
        "\xE2\x81\xA9", // U+2069 PDI
        "\xD8\x9C",     // U+061C ALM
    };

    std::string out = s;
    for (const char* ctrl : kBidiControls)
    {
        size_t pos;
        while ((pos = out.find(ctrl)) != std::string::npos)
            out.erase(pos, strlen(ctrl));
    }
    return out;
}

// =============================================================================
// Server name/game mode - read directly from the network's player-info list rather than
// CGameCtnNetServerInfo, following the same technique AroPix/ForeverRPC uses (src/game.rs,
// get_player_rows()/get_online_game_mode_name()): the server itself always shows up as one entry
// in that same list (uid == 0), so its nickname can be read from there instead of chasing a
// separate, less-understood struct. Every read goes through Twinkie->TryRead()/IsReadableMemory()
// rather than a raw Read<T>() - same crash-avoidance reasoning as TwinkTrackmania::GetChallenge()
// etc, since this walks a buffer of pointers that's exactly the kind of thing another injected DLL
// (TMUnlimiter) can transiently invalidate. Server login isn't read (no current need for it - it
// was only for a "Join Server" button, dropped since Discord buttons never show to the local user
// anyway, so there was no way to confirm it actually worked).
// =============================================================================
static std::string ReadFastStringW(TwinkTrackmania* twinkie, uintptr_t obj, uintptr_t offset)
{
    TM::CFastStringInt fs{};
    if (!twinkie->TryRead<TM::CFastStringInt>(obj + offset, fs)) return "";
    if (fs.Size <= 0 || fs.Size > 256 || !fs.Cstr) return "";
    if (!twinkie->IsReadableMemory((uintptr_t)fs.Cstr, (size_t)fs.Size * sizeof(wchar_t))) return "";
    return twinkie->WStringToUTF8(std::wstring(fs.Cstr, (size_t)fs.Size));
}

// PrimaryType-style raw mode byte read from CGameCtnNetServerInfo - same offsets ForeverRPC uses
// (network + 0x23C for the server info pointer, +0x200 for the raw mode).
static std::string OnlineGameModeName(unsigned int rawMode)
{
    switch (rawMode)
    {
        case 1: return "Time Attack";
        case 3: return "Rounds";
        case 6: return "Team";
        case 7: return "Laps";
        case 8: return "Stunts";
        case 9: return "Cup";
        default: return "";
    }
}

// =============================================================================
// Live game-state hook - AroPix/ForeverRPC's install_game_state_hook()/latest_game_state()
// (src/hook.rs) ported to use MinHook (already linked via kiero) instead of a hand-rolled
// trampoline. Detours GameNetwork's state-change function so every transition is captured the
// instant it happens, used as a fallback when a direct memory read of the current state comes back
// empty (e.g. between frames, or if the direct read's own pointer chain is momentarily invalid).
// =============================================================================
static std::atomic<unsigned int> s_LastGameState{ 0 };
static std::atomic<bool>         s_GameStateHookInstalled{ false };

using ChangeStateFn = void(__thiscall*)(void*, unsigned int, int);
static ChangeStateFn s_OriginalChangeState = nullptr;

// MSVC won't let a free function be defined with __thiscall directly - __fastcall is the standard
// workaround (it also passes the first argument in ECX, same as __thiscall's "this"; the unused
// EDX parameter absorbs what __fastcall would otherwise put there for a second register argument).
static void __fastcall ChangeStateDetour(void* self, void* /*unusedEdx*/, unsigned int newState, int sendServerInfo)
{
    s_LastGameState.store(newState, std::memory_order_release);
    if (s_OriginalChangeState) s_OriginalChangeState(self, newState, sendServerInfo);
}

// Disabled - a user got stuck on the profile-select screen shortly after this was added, which is
// exactly the symptom of patching the wrong function: IsReadableMemory() only confirms the RVA
// points at *some* valid code, not that it's actually GameNetwork::ChangeState in this specific
// game build (the same RVA-drift problem already caught once this session for the server-name
// offset, but this time the miss is a code patch instead of a read - far higher blast radius, since
// a wrong hook can corrupt any state transition that happens to run through whatever it actually
// landed on). GameStateName() below still works fine without it via the direct memory reads;
// {game_state_name} just quietly stays empty on the rare case those come back empty too, instead of
// falling back to a hook that isn't confirmed safe.
static void InstallGameStateHookOnce(TwinkTrackmania* twinkie)
{
    (void)twinkie;
}

// Richer than the plain playing/not-playing check elsewhere in this file - distinguishes menus,
// the editor, an active race, watching a replay, etc. Same raw state values and offsets
// ForeverRPC's get_game_state_name()/read_current_game_state_raw() use.
static std::string GameStateName(unsigned int rawState)
{
    switch (rawState)
    {
        case 0x1:     return "In Menus (Online)";
        case 0x2:     return "Preparing Round";
        case 0x4:     return "Racing Online";
        case 0x8:     return "Round Ended";
        case 0x10:    return "Starting Up";
        case 0x20:    return "In Menus";
        case 0x40:    return "Quitting";
        case 0x80:    return "Loading";
        case 0x100:   return "In Map Editor";
        case 0x200:   return "Racing";
        case 0x400:   return "Race Finished";
        case 0x1000:  return "Watching Replay";
        case 0x800:   return "In Replay Editor";
        case 0x2000:  return "Race Ended";
        case 0x4000:  return "Syncing";
        case 0x8000:  return "Playing Online";
        case 0x10000: return "Leaving Round";
        default:      return "";
    }
}

static std::string GetGameStateName(TwinkTrackmania* twinkie)
{
    InstallGameStateHookOnce(twinkie);

    unsigned int rawState = 0;
    bool found = false;

    uintptr_t network = twinkie->GetNetwork();
    if (network)
    {
        // Local player's own info pointer (Network+0x54) first - direct and cheap.
        uintptr_t localPlayerInfo = 0;
        if (twinkie->TryRead<uintptr_t>(network + 0x54, localPlayerInfo) && localPlayerInfo)
            found = twinkie->TryRead<unsigned int>(localPlayerInfo + 0x124, rawState);

        // Fall back to the first entry of the network's player-info buffer - same hardcoded 0x2FC
        // ForeverRPC's own read_current_game_state_raw() falls back to here too (its reflection
        // resolver is only used for the fuller player-row enumeration elsewhere, not this path).
        if (!found)
        {
            TM::CFastBuffer<uintptr_t> buffer{};
            if (twinkie->TryRead<TM::CFastBuffer<uintptr_t>>(network + 0x2FC, buffer) &&
                buffer.Size > 0 && buffer.Ptr && twinkie->IsReadableMemory((uintptr_t)buffer.Ptr, sizeof(uintptr_t)))
            {
                uintptr_t player0 = 0;
                if (twinkie->TryRead<uintptr_t>((uintptr_t)buffer.Ptr, player0) && player0)
                    found = twinkie->TryRead<unsigned int>(player0 + 0x124, rawState);
            }
        }
    }

    // Last resort - whatever the live hook last saw, in case both direct reads above came back
    // empty (e.g. mid-transition).
    if (!found)
    {
        rawState = s_LastGameState.load(std::memory_order_acquire);
        found = (rawState != 0);
    }

    return found ? GameStateName(rawState) : "";
}

// This used to resolve the "PlayerInfos" field offset via the game's own class reflection
// (calling the object's GetClassInfo virtual, then walking its member-info arrays) rather than
// trusting a hardcoded offset. That reflection path is almost certainly what caused the
// profile-select freeze: unlike every other read in this file (TryRead()/IsReadableMemory(),
// which only ever inspect data), it actually CALLS a function pointer read out of memory - real
// code execution inside the game process, every single frame until it first succeeds. Disabling
// the state hook alone didn't fix the freeze (confirmed by testing), but this call is a much
// closer match: it fires from RenderAnyways() every frame during exactly the pre-login window
// where the network object is in a less-than-fully-initialized state, and unlike a bad data
// read, a bad function-pointer call has no safety net - it can hang or corrupt state instead of
// just failing cleanly. Replaced with the plain hardcoded offset (still only ever read through
// TryRead()/IsReadableMemory(), never called into) so this file no longer executes any code it
// didn't write itself.
static uintptr_t GetPlayerInfosOffset(TwinkTrackmania* twinkie, uintptr_t network)
{
    (void)twinkie;
    (void)network;
    return 0x2FC;
}

struct ServerPresenceInfo
{
    bool        onServer   = false;
    int         playerCount = 0;
    std::string name;
    std::string modeName;
};

static ServerPresenceInfo GetServerPresenceInfo(TwinkTrackmania* twinkie)
{
    ServerPresenceInfo info;

    uintptr_t network = twinkie->GetNetwork();
    if (!network) return info;

    uintptr_t playerInfosOffset = GetPlayerInfosOffset(twinkie, network);

    TM::CFastBuffer<uintptr_t> buffer{};
    if (!twinkie->TryRead<TM::CFastBuffer<uintptr_t>>(network + playerInfosOffset, buffer)) return info;
    if (buffer.Size == 0 || buffer.Size > 2048 || !buffer.Ptr) return info;
    if (!twinkie->IsReadableMemory((uintptr_t)buffer.Ptr, buffer.Size * sizeof(uintptr_t))) return info;

    // Counts every valid row (including the server's own uid==0 pseudo-row), then subtracts 1 for
    // it below - same approach ForeverRPC's own player_count uses (rows.len().checked_sub(1)).
    int validRowCount = 0;

    for (size_t i = 0; i < buffer.Size; i++)
    {
        uintptr_t obj = 0;
        if (!twinkie->TryRead<uintptr_t>((uintptr_t)(buffer.Ptr + i), obj)) continue;
        if (!obj || !twinkie->IsReadableMemory(obj, 0x340)) continue;

        validRowCount++;

        unsigned char uid = 0;
        if (!twinkie->TryRead<unsigned char>(obj + 0x24, uid)) continue;
        if (uid != 0) continue; // uid 0 is always the server itself, not a player

        unsigned int playerType = 0;
        twinkie->TryRead<unsigned int>(obj + 0x148, playerType);

        info.onServer = (playerType == 2);
    }

    info.playerCount = validRowCount > 0 ? validRowCount - 1 : 0;

    if (info.onServer)
    {
        uintptr_t serverInfo = 0;
        if (twinkie->TryRead<uintptr_t>(network + 0x23C, serverInfo) && serverInfo)
        {
            // The friendly server name (e.g. "FLIP") lives here, not on the player row's nick
            // field ForeverRPC itself reads - found by scanning this build's CGameCtnNetServerInfo
            // for a plausible string and confirming which offset actually matched a live server.
            info.name = StripBidiControls(StripTmFormatting(ReadFastStringW(twinkie, serverInfo, 0xB8)));

            unsigned int rawMode = 0;
            if (twinkie->TryRead<unsigned int>(serverInfo + 0x200, rawMode))
                info.modeName = OnlineGameModeName(rawMode);
        }
    }

    return info;
}

static bool ContainsCaseInsensitive(const std::string& haystack, const std::string& needle)
{
    if (needle.empty() || haystack.size() < needle.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(),
        [](unsigned char a, unsigned char b) { return tolower(a) == tolower(b); });
    return it != haystack.end();
}

// Percent-encodes a string for use in a URL query parameter (RFC 3986 unreserved chars pass
// through as-is, everything else becomes %XX).
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

static std::string JsonEscape(const std::string& s)
{
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else out.push_back((char)c);
        }
    }
    return out;
}

// =============================================================================
// Minimal TMX lookup for non-RMC maps - same WinHTTP GET + hand-rolled single-field JSON
// extraction pattern already proven in Randomizer.cpp, kept self-contained here rather than
// sharing code across modules.
// =============================================================================
static bool TmxHttpGet(const std::string& host, const std::string& path, std::string& outBody)
{
    int wlenHost = MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, nullptr, 0);
    std::wstring whost(wlenHost, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, host.c_str(), -1, whost.data(), wlenHost);
    if (!whost.empty() && whost.back() == L'\0') whost.pop_back();

    int wlenPath = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, nullptr, 0);
    std::wstring wpath(wlenPath, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), -1, wpath.data(), wlenPath);
    if (!wpath.empty() && wpath.back() == L'\0') wpath.pop_back();

    HINTERNET hSess = WinHttpOpen(L"TwinkDiscordRP/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSess) return false;

    HINTERNET hConn = WinHttpConnect(hSess, whost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", wpath.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    bool ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
              WinHttpReceiveResponse(hReq, nullptr);
    if (ok)
    {
        DWORD avail = 0, read = 0;
        std::vector<char> data;
        while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0)
        {
            size_t off = data.size();
            data.resize(off + avail);
            WinHttpReadData(hReq, data.data() + off, avail, &read);
            data.resize(off + read);
        }
        outBody.assign(data.begin(), data.end());
    }

    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess);
    return ok;
}

static bool TmxJsonFindInt(const std::string& json, const std::string& key, long long& outVal)
{
    std::string pattern = "\"" + key + "\":";
    size_t pos = json.find(pattern);
    if (pos == std::string::npos) return false;
    pos += pattern.size();

    long long val = 0;
    bool any = false;
    while (pos < json.size() && isdigit((unsigned char)json[pos]))
    {
        val = val * 10 + (json[pos] - '0');
        pos++;
        any = true;
    }
    if (!any) return false;
    outVal = val;
    return true;
}

void TwinkDiscordRPModule::TmxLookupWorker(std::string mapName)
{
    static const char* kHosts[2] = { "tmnf.exchange", "tmuf.exchange" };
    std::string foundUrl;

    for (const char* host : kHosts)
    {
        std::string path = "/api/tracks?name=" + UrlEncode(mapName) + "&count=1&fields=TrackId";
        std::string body;
        if (!TmxHttpGet(host, path, body)) continue;

        long long trackId = 0;
        if (TmxJsonFindInt(body, "TrackId", trackId) && trackId > 0)
        {
            foundUrl = "https://" + std::string(host) + "/trackshow/" + std::to_string(trackId);
            break;
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_LookupMutex);
        m_LookupResultUrl     = foundUrl;
        m_LookupResultMapName = mapName;
        m_LookupResultReady   = true;
    }
    m_LookupInProgress = false; // last, so the main thread never sees this before the result above
}

void TwinkDiscordRPModule::StartTmxLookup(const std::string& mapName)
{
    // A previous lookup can still be in flight (tmnf.exchange/tmuf.exchange being slow, or just
    // unresponsive) when the map changes again - joinable() alone can't tell "still running" from
    // "finished, just needs cleanup", so calling join() unconditionally here used to block this
    // (main/render) thread for however long that HTTP request took to time out, freezing the whole
    // game. m_LookupInProgress makes that distinction: if a lookup is still running, skip starting
    // a new one entirely this frame rather than waiting on it - the next map-name check will try
    // again once it's actually done.
    if (m_LookupInProgress.load()) return;
    if (m_LookupThread.joinable()) m_LookupThread.join(); // safe: only reached once the flag says done
    m_LookupInProgress = true;
    m_LookupForMapName = mapName;
    m_LookupThread = std::thread(&TwinkDiscordRPModule::TmxLookupWorker, this, mapName);
}

// =============================================================================
// Raw Discord IPC client. Discord's desktop app listens on a local named pipe
// (\\.\pipe\discord-ipc-0, or -1.. -9 if multiple clients/accounts are involved) using a small
// framed protocol: a 4-byte little-endian opcode, a 4-byte little-endian payload length, then the
// JSON payload itself. This is the same protocol the official SDKs (including the GameSDK we
// used previously) talk over - implemented directly here because the GameSDK's last-ever build
// has no support for Rich Presence buttons, while this protocol does.
//   Opcode 0 = Handshake  (client -> server, once, right after connecting)
//   Opcode 1 = Frame      (either direction - all commands/events after the handshake)
//   Opcode 2 = Close
// =============================================================================
static constexpr unsigned int kIpcOpHandshake = 0;
static constexpr unsigned int kIpcOpFrame     = 1;

bool TwinkDiscordRPModule::SendFrame(unsigned int opcode, const std::string& json)
{
    if (!m_Pipe) return false;

    unsigned int header[2] = { opcode, (unsigned int)json.size() };
    DWORD written = 0;
    if (!WriteFile((HANDLE)m_Pipe, header, sizeof(header), &written, NULL) || written != sizeof(header))
        return false;
    if (!json.empty())
    {
        if (!WriteFile((HANDLE)m_Pipe, json.data(), (DWORD)json.size(), &written, NULL) || written != json.size())
            return false;
    }
    return true;
}

// Plain ReadFile() on a pipe has no timeout - if whatever's on the other end never replies, this
// hangs the render thread forever. Poll with PeekNamedPipe (non-blocking) until the bytes are
// actually there before reading for real.
static bool WaitForPipeData(HANDLE pipe, DWORD neededBytes, DWORD timeoutMs)
{
    auto start = std::chrono::steady_clock::now();
    for (;;)
    {
        DWORD available = 0;
        if (!PeekNamedPipe(pipe, NULL, 0, NULL, &available, NULL)) return false;
        if (available >= neededBytes) return true;
        if (std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() >= timeoutMs)
            return false;
        Sleep(10);
    }
}

bool TwinkDiscordRPModule::ConnectPipe()
{
    for (int i = 0; i < 10; i++)
    {
        std::string pipeName = "\\\\.\\pipe\\discord-ipc-" + std::to_string(i);
        HANDLE pipe = CreateFileA(pipeName.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (pipe == INVALID_HANDLE_VALUE) continue;

        m_Pipe = pipe;

        std::string handshake = "{\"v\":1,\"client_id\":\"" + std::to_string(kDiscordAppId) + "\"}";
        if (!SendFrame(kIpcOpHandshake, handshake))
        {
            DisconnectPipe();
            continue;
        }

        // Read the handshake response (READY event) to confirm Discord actually accepted us.
        unsigned int header[2] = {};
        DWORD readBytes = 0;
        if (!WaitForPipeData(pipe, sizeof(header), 1500) ||
            !ReadFile(pipe, header, sizeof(header), &readBytes, NULL) || readBytes != sizeof(header))
        {
            DisconnectPipe();
            continue;
        }

        std::string response(header[1], '\0');
        if (header[1] > 0)
        {
            if (!WaitForPipeData(pipe, header[1], 1500) ||
                !ReadFile(pipe, response.data(), header[1], &readBytes, NULL) || readBytes != header[1])
            {
                DisconnectPipe();
                continue;
            }
        }

        if (response.find("\"READY\"") == std::string::npos)
        {
            DisconnectPipe();
            continue;
        }

        m_Connected         = true;
        m_StartTimestampSec = (long long)time(nullptr);
        Logger->PrintInternal("[Discord RP] Connected to Discord.");
        return true;
    }

    return false;
}

void TwinkDiscordRPModule::DisconnectPipe()
{
    // Explicitly clear the activity before closing the handle, rather than just relying on Discord
    // to notice the pipe died: on a normal close the OS closes every handle anyway (that part of
    // process cleanup happens even under ExitProcess(), unlike DLL_PROCESS_DETACH), but Discord's
    // own reconnect/timeout handling can take a while to notice and blank the status - "still
    // showing as Playing minutes after quitting" was exactly that gap. A real SET_ACTIVITY with a
    // null activity clears it immediately instead of waiting on that timeout.
    if (m_Connected)
    {
        std::string nonce = "twinkplanet-" + std::to_string(++m_NonceCounter);
        std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
            ",\"activity\":null},\"nonce\":\"" + nonce + "\"}";
        SendFrame(kIpcOpFrame, payload);
    }

    if (m_Pipe)
    {
        CloseHandle((HANDLE)m_Pipe);
        m_Pipe = nullptr;
    }
    m_Connected = false;
}

TwinkDiscordRPModule::~TwinkDiscordRPModule()
{
    DisconnectPipe();
    if (m_LookupThread.joinable()) m_LookupThread.join();
}

void TwinkDiscordRPModule::UpdatePresence()
{
    if (!m_Connected) return;

    // Drain anything Discord sent back (command acks, etc.) - PeekNamedPipe is non-blocking, so
    // this never stalls the render thread. Left unread, these would slowly fill the pipe's
    // buffer over a long session and eventually make writes fail.
    DWORD available = 0;
    if (PeekNamedPipe((HANDLE)m_Pipe, NULL, 0, NULL, &available, NULL) && available > 0)
    {
        std::string discardBuf(available, '\0');
        DWORD readBytes = 0;
        if (ReadFile((HANDLE)m_Pipe, discardBuf.data(), available, &readBytes, NULL) &&
            discardBuf.find("\"ERROR\"") != std::string::npos)
        {
            Logger->PrintErrorArgs("[Discord RP] Discord rejected an update: {}", discardBuf);
        }
    }

    bool        playing = Twinkie->IsPlaying();
    // The raw (formatting-stripped, un-templated) map name - used for internal comparisons (RMC
    // track matching, TMX lookup key) so a customized details template's extra wording can never
    // throw those off. The user-facing "details" text is built from this further down.
    std::string rawMapName = playing ? StripBidiControls(StripTmFormatting(Twinkie->GetChallengeName())) : "";

    // Distinguishes menus/editor/race/replay/etc - independent of the on/off "playing" check above.
    std::string gameStateName = GetGameStateName(Twinkie);

    // HasCurrentTrack() only means RMC has *a* track queued/ready - it says nothing about whether
    // the player is actually in it right now (could've alt-tabbed to load something else while
    // RMC sat there ready). Comparing the live challenge name against RMC's own track name is the
    // same check CheckLoadFailure() already relies on internally to confirm a load went through.
    bool isRmcTrack = playing && m_Randomizer && m_Randomizer->HasCurrentTrack()
                    && ContainsCaseInsensitive(rawMapName, StripTmFormatting(m_Randomizer->GetCurrentTrackName()));

    // Only actually walks the player list (and only then looks up the game mode) while playing -
    // no point paying for it while sitting in menus.
    ServerPresenceInfo server = playing ? GetServerPresenceInfo(Twinkie) : ServerPresenceInfo{};

    auto ApplyPlaceholders = [&](const std::string& tmpl) -> std::string
    {
        std::string result = tmpl;
        result = ReplacePlaceholder(result, "{map_name}",        rawMapName);
        result = ReplacePlaceholder(result, "{server_name}",     server.name);
        result = ReplacePlaceholder(result, "{mode_name}",       server.modeName);
        result = ReplacePlaceholder(result, "{player_count}",    std::to_string(server.playerCount));
        result = ReplacePlaceholder(result, "{game_state_name}", gameStateName);
        return result;
    };

    // Each template field is a plain user-edited string in Settings, empty by default - an empty
    // field falls back to the original fixed text below, so leaving this section untouched keeps
    // today's exact presence text - except "Playing a map" while actually on a server, where the
    // richer "Playing a Map on {server_name}" default kicks in automatically (still overridden
    // entirely by a custom template, same as every other field).
    bool onServerWithName = server.onServer && !server.name.empty();
    std::string state = ApplyPlaceholders(!playing
        ? (m_TemplateMenusBuf[0] ? m_TemplateMenusBuf : kDefaultTemplateMenus)
        : (isRmcTrack
            ? (m_TemplateRmcBuf[0] ? m_TemplateRmcBuf : kDefaultTemplateRmc)
            : (onServerWithName
                ? (m_TemplateMapServerBuf[0] ? m_TemplateMapServerBuf : kDefaultTemplateMapOnServer)
                : (m_TemplateMapBuf[0]       ? m_TemplateMapBuf       : kDefaultTemplateMap))));

    // RMC tracks get an exact link (GetCurrentTmxUrl() - a real TrackId). An official Nadeo
    // campaign track (IsOfficial()) skips TMX entirely instead - its authorship is always Nadeo,
    // so there's nothing to look up, and a name search risks landing on an unrelated community
    // reupload sharing the same title (e.g. the official "A01-Race" vs someone's custom-uploaded
    // "A01-Race Race"). Any other map (loaded locally, from a server, etc.) has no known TrackId,
    // so a background lookup searches TMX by name instead - both tmnf.exchange AND tmuf.exchange
    // regardless of which one this build is, since a map can be cross-listed on either. Not
    // guaranteed to find the exact right map, but better than nothing when it lands. There's no
    // way to read the map's author from game memory to narrow the search further, and TMX itself
    // ties authorship to an internal numeric user ID rather than the raw in-game login anyway.
    bool isOfficial = playing && Twinkie->IsOfficial();
    std::string tmxUrl;
    if (isOfficial)
    {
        m_CurrentFallbackUrl.clear();
        m_LookupForMapName.clear();
    }
    else
    {
        // Same isRmcTrack check as the state text above - GetCurrentTmxUrl() only checks whether
        // RMC has a track queued, not whether the player is actually in it right now.
        tmxUrl = isRmcTrack ? m_Randomizer->GetCurrentTmxUrl() : "";
        if (tmxUrl.empty() && !rawMapName.empty())
        {
            if (rawMapName != m_LookupForMapName) StartTmxLookup(rawMapName);

            if (m_LookupResultReady)
            {
                std::lock_guard<std::mutex> lock(m_LookupMutex);
                if (m_LookupResultMapName == rawMapName) m_CurrentFallbackUrl = m_LookupResultUrl;
                m_LookupResultReady = false;
            }

            if (m_LookupForMapName == rawMapName) tmxUrl = m_CurrentFallbackUrl;
        }
        else if (tmxUrl.empty())
        {
            m_CurrentFallbackUrl.clear();
            m_LookupForMapName.clear();
        }
    }

    // Built from rawMapName (not the templated text) so an empty map name always stays empty
    // regardless of template wording - matters since Discord requires details/state to be
    // empty-or->=2 chars, and a static template prefix could otherwise turn "" into non-empty text.
    std::string details;
    if (!rawMapName.empty())
    {
        const char* detailsTemplate = isOfficial
            ? (m_TemplateOfficialBuf[0] ? m_TemplateOfficialBuf : kDefaultTemplateOfficial)
            : (m_TemplateDetailsBuf[0]  ? m_TemplateDetailsBuf  : kDefaultTemplateDetails);
        details = ApplyPlaceholders(detailsTemplate);
    }

    std::string activityJson = "{";
    activityJson += "\"state\":\"" + JsonEscape(state) + "\"";
    if (details.size() >= 2) // Discord requires details/state to be empty-or->=2 chars
        activityJson += ",\"details\":\"" + JsonEscape(details) + "\"";
    activityJson += ",\"timestamps\":{\"start\":" + std::to_string(m_StartTimestampSec) + "}";
    activityJson += ",\"assets\":{\"large_image\":\"" + std::string(kLargeImageKey) + "\",\"large_text\":\"TwinkPlanet\"}";
    activityJson += ",\"instance\":false";

    if (!tmxUrl.empty())
        activityJson += ",\"buttons\":[{\"label\":\"Open In Browser\",\"url\":\"" + JsonEscape(tmxUrl) + "\"}]";
    activityJson += "}";

    // Every SET_ACTIVITY needs its own nonce - reference IPC client implementations (e.g.
    // jagrosh/DiscordIPC) generate a fresh one (a random UUID there) per call; reusing the same
    // literal string for every update risked Discord's client treating repeats as a duplicate of
    // the first request and silently not reapplying fields like buttons on later updates.
    std::string nonce = "twinkplanet-" + std::to_string(++m_NonceCounter);
    std::string payload = "{\"cmd\":\"SET_ACTIVITY\",\"args\":{\"pid\":" + std::to_string(GetCurrentProcessId()) +
        ",\"activity\":" + activityJson + "},\"nonce\":\"" + nonce + "\"}";

    if (!SendFrame(kIpcOpFrame, payload))
    {
        // Discord likely closed the pipe (app restarted/closed) - drop the connection so the
        // next tick can retry from scratch.
        DisconnectPipe();
    }
}

void TwinkDiscordRPModule::RenderAnyways()
{
    // Used to force-disable until login, working around the profile-select freeze. That freeze's
    // real cause was tracked down to GetPlayerInfosOffset()'s old reflection path making a live
    // function-pointer call into the game every frame pre-login (see the comment there) - now
    // fixed at the source, so Enabled no longer needs to be second-guessed here based on login
    // state; it's just the user's actual toggle.
    if (!Enabled)
    {
        if (m_Connected) DisconnectPipe();
        return;
    }

    if (!m_Connected)
    {
        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(now - m_LastConnectAttempt).count() < 5.0) return;
        m_LastConnectAttempt = now;
        if (!ConnectPipe()) return;
    }

    // TMUnlimiter bundles its own Discord RPC integration too (its discord_game_sdk.dll ships
    // right next to TMUnlimiter.dll), and Discord only ever shows
    // whichever app most recently updated its activity - there's no real "priority" to win, so
    // updating more often just means ours is more often the last one in and wins more often.
    // Discord's own guidance is "no more than once every 15s" for the HTTP API; that limit doesn't
    // apply to the local IPC pipe, so this stays well under 1s specifically to out-race
    // TMUnlimiter's own update loop, which was otherwise winning and clobbering ours.
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - m_LastUpdate).count() < 0.5) return;
    m_LastUpdate = now;

    UpdatePresence();
}

void TwinkDiscordRPModule::RenderMenuItem()
{
    using namespace ImGui;

    if (MenuItem(ICON_FK_DISCORD_ALT " Discord Rich Presence", "", Enabled))
        Enabled = !Enabled;
}

void TwinkDiscordRPModule::RenderSettings()
{
    using namespace ImGui;

    TextWrapped("Shows your current TrackMania activity (in a menu, or playing a map and which "
                "one) as your Discord Rich Presence status, with an Open In Browser button "
                "linking the current RMC track's TMX page.");
    TextWrapped("Talks directly to Discord's local IPC pipe (the same protocol its official SDKs "
                "use) rather than bundling Discord's GameSDK, since that SDK has no support for "
                "Rich Presence buttons.");

    Separator();

    if (m_Connected)
        TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "Connected.");
    else
        TextDisabled("Not connected - retrying every few seconds (is Discord running?).");

    if (Twinkie->IsPlaying())
    {
        ServerPresenceInfo debugServer = GetServerPresenceInfo(Twinkie);
        if (debugServer.onServer)
            TextColored({ 0.3f, 1.f, 0.3f, 1.f }, "On server: %s%s%s",
                debugServer.name.c_str(),
                debugServer.modeName.empty() ? "" : " (",
                debugServer.modeName.empty() ? "" : (debugServer.modeName + ")").c_str());
        else
            TextDisabled("Not on a server right now.");
    }

    Separator();

    if (CollapsingHeader("Customize presence text"))
    {
        TextWrapped("Leave any field blank to keep the default shown as its hint - nothing here "
                    "is required. \"Playing a map\" already switches to \"Playing a Map on "
                    "{server_name}\" by itself while on a server, with no setup needed.");
        TextWrapped("Placeholders: {map_name}, {game_state_name}, {server_name}, {mode_name}, "
                    "{player_count} - the last three are only filled in while actually on a server.");

        SetNextItemWidth(200.f);
        InputTextWithHint("In menus", kDefaultTemplateMenus, m_TemplateMenusBuf, sizeof(m_TemplateMenusBuf));

        SetNextItemWidth(200.f);
        InputTextWithHint("Playing RMC", kDefaultTemplateRmc, m_TemplateRmcBuf, sizeof(m_TemplateRmcBuf));

        SetNextItemWidth(200.f);
        InputTextWithHint("Playing a map", kDefaultTemplateMap, m_TemplateMapBuf, sizeof(m_TemplateMapBuf));

        SetNextItemWidth(300.f);
        InputTextWithHint("Playing on a server", kDefaultTemplateMapOnServer, m_TemplateMapServerBuf, sizeof(m_TemplateMapServerBuf));
        if (IsItemHovered())
            SetTooltip("Used instead of \"Playing a map\" while actually connected to a server.\n{server_name}, {mode_name}, and {player_count} are filled in here.");

        SetNextItemWidth(300.f);
        InputTextWithHint("Map details", kDefaultTemplateDetails, m_TemplateDetailsBuf, sizeof(m_TemplateDetailsBuf));

        SetNextItemWidth(300.f);
        InputTextWithHint("Map details (official)", kDefaultTemplateOfficial, m_TemplateOfficialBuf, sizeof(m_TemplateOfficialBuf));
        if (IsItemHovered())
            SetTooltip("Used instead of \"Map details\" for official Nadeo campaign tracks.");

        if (Button(ICON_FK_REFRESH " Reset to defaults"))
        {
            m_TemplateMenusBuf[0]     = '\0';
            m_TemplateRmcBuf[0]       = '\0';
            m_TemplateMapBuf[0]       = '\0';
            m_TemplateMapServerBuf[0] = '\0';
            m_TemplateDetailsBuf[0]   = '\0';
            m_TemplateOfficialBuf[0]  = '\0';
        }
    }
}

void TwinkDiscordRPModule::SettingsInit(SettingMgr& Settings)
{
    Settings["DiscordRP"]["Enable"].GetAsBool(&Enabled);

    auto LoadTemplate = [&](const char* key, char* buf, size_t bufSize)
    {
        std::string value;
        Settings["DiscordRP"][key].GetAsString(&value);
        strncpy_s(buf, bufSize, value.c_str(), _TRUNCATE);
    };
    LoadTemplate("TemplateMenus",     m_TemplateMenusBuf,     sizeof(m_TemplateMenusBuf));
    LoadTemplate("TemplateRmc",       m_TemplateRmcBuf,       sizeof(m_TemplateRmcBuf));
    LoadTemplate("TemplateMap",       m_TemplateMapBuf,       sizeof(m_TemplateMapBuf));
    LoadTemplate("TemplateMapServer", m_TemplateMapServerBuf, sizeof(m_TemplateMapServerBuf));
    LoadTemplate("TemplateDetails",   m_TemplateDetailsBuf,   sizeof(m_TemplateDetailsBuf));
    LoadTemplate("TemplateOfficial",  m_TemplateOfficialBuf,  sizeof(m_TemplateOfficialBuf));
}

void TwinkDiscordRPModule::SettingsSave(SettingMgr& Settings)
{
    Settings["DiscordRP"]["Enable"].Set(Enabled);
    Settings["DiscordRP"]["TemplateMenus"].Set(std::string(m_TemplateMenusBuf));
    Settings["DiscordRP"]["TemplateRmc"].Set(std::string(m_TemplateRmcBuf));
    Settings["DiscordRP"]["TemplateMap"].Set(std::string(m_TemplateMapBuf));
    Settings["DiscordRP"]["TemplateMapServer"].Set(std::string(m_TemplateMapServerBuf));
    Settings["DiscordRP"]["TemplateDetails"].Set(std::string(m_TemplateDetailsBuf));
    Settings["DiscordRP"]["TemplateOfficial"].Set(std::string(m_TemplateOfficialBuf));
}
