#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace dfts {

enum class Language { English, Portuguese };

// User settings + session token, persisted to %LOCALAPPDATA%\DFTwitchStream\config.json.
// The OAuth token is stored DPAPI-encrypted (per user account) rather than in plain text.
struct Settings {
    Language    language        = Language::English;

    // Twitch application
    std::string clientId;                    // empty => built-in public client id
    std::string accessToken;                 // user token (decrypted in memory)
    std::string refreshToken;
    std::string userLogin;
    std::string userId;
    std::string userDisplayName;
    int64_t     tokenExpiry     = 0;

    // Playback
    // 0 = libVLC (bundled, runs in-process), 1 = mpv (external executable)
    int         playerBackend   = 0;
    std::string mpvPath;                     // "" => auto-detect
    std::string ffmpegPath;                  // "" => auto-detect
    std::string preferredQuality = "best";   // best / 1080p60 / 720p60 / 720p / 480p / audio_only
    bool        lowLatency       = true;
    int         volume           = 80;       // 0..100

    // Chat
    bool        chatEnabled      = true;
    int         chatWidth        = 360;      // px
    bool        chatOnRight      = true;
    bool        chatTimestamps   = true;
    // Emotes from 7TV, BetterTTV and FrankerFaceZ, which most chats use.
    bool        thirdPartyEmotes = true;
    int         chatFontScale    = 100;      // percent

    // Downloads
    std::string downloadDir;                 // "" => Videos\DFTwitchStream
    int         downloadThreads  = 8;
    bool        muxWithFfmpeg    = true;
    bool        keepSegments     = false;

    // UI
    // Two-letter code used to filter Browse; empty means every language.
    std::string browseLanguage;
    bool        showOfflineChannels = false;
    int         gridColumns         = 4;
    float       uiScale             = 1.0f;
    bool        startMaximized      = true;

    // Recovery
    std::vector<std::string> recentRecoveryUrls;

    bool loggedIn() const { return !accessToken.empty() && !userId.empty(); }
};

class Config {
public:
    static Config& get();

    Settings&       settings()       { return m_settings; }
    const Settings& settings() const { return m_settings; }

    bool load();
    bool save();

    std::string path() const { return m_path; }

    // Effective values with fallbacks applied.
    std::string effectiveClientId() const;
    std::string effectiveDownloadDir() const;

    void clearSession();

private:
    Config();
    std::string m_path;
    Settings    m_settings;
    std::mutex  m_mutex;
};

// DPAPI helpers (CurrentUser scope). Returns "" on failure.
std::string protectString(const std::string& plain);
std::string unprotectString(const std::string& base64Blob);

} // namespace dfts
