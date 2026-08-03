#pragma once
#include "core/Config.h"
#include "twitch/TwitchApi.h"
#include "twitch/TwitchAuth.h"
#include "twitch/TwitchGql.h"
#include "vod/VodRecovery.h"
#include "vod/Downloader.h"
#include "app/WindowChrome.h"
#include "player/MpvHost.h"
#include "player/VlcPlayer.h"
#include "player/PlaybackQueue.h"
#include "chat/ChatClient.h"
#include "chat/ThirdPartyEmotes.h"
#include "ui/Fonts.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

struct GLFWwindow;

namespace dfts {

enum class Screen { Login, Following, Browse, Vods, Recovery, Downloads, Watch, Settings, Logs, About };

// A transient message shown in the bottom-right corner.
struct Toast {
    std::string text;
    ImU32       color;
    double      expiresAt = 0.0;
};

class App {
public:
    App();
    ~App();

    bool init();
    int  run();
    void shutdown();

private:
    // --- frame ------------------------------------------------------------
    void drawFrame();
    void drawTitleBar();
    void drawSidebar();
    void drawTopBar();
    void drawContent();
    void drawToasts();
    void drawModals();

    // --- screens (one translation unit each) -------------------------------
    void drawLogin();
    void drawFollowing();
    void drawBrowse();
    void drawVods();
    void drawVodsTable();
    void drawClipsTab();
    void drawRecovery();
    void drawDownloads();
    void drawWatch();
    void drawSettings();
    void drawLogs();
    void drawAbout();

    // --- helpers used by the screens ---------------------------------------
    void   setScreen(Screen s);
    // Fullscreen only makes sense on the Watch screen; entering from anywhere
    // else switches to it first.
    void   toggleFullscreen();
    bool   fullscreen() const;
    void   toast(const std::string& text, ImU32 color);
    void   toastOk(const std::string& text);
    void   toastError(const std::string& text);

    // Runs `work` on a detached thread. Anything touching UI state must be
    // handed back through post().
    void   runAsync(std::function<void()> work);
    // Queues a callable to run on the UI thread at the start of the next frame.
    void   post(std::function<void()> fn);
    void   drainTasks();

    twitch::Api makeApi() const;
    bool   requireLogin();
    void   applyLanguage(Language lang);
    void   rebuildFonts();

    // --- actions ------------------------------------------------------------
    void   startLogin();
    void   cancelLogin();
    void   logout();
    void   restoreSession();

    void   refreshFollowing();
    void   loadVodsForChannel(const std::string& login);
    void   loadClipsForChannel(const std::string& login);
    void   downloadClip(const twitch::Clip& clip);
    void   startRecoveryScan();

    void   watchChannel(const std::string& login, const std::string& displayName);
    void   watchVod(const twitch::Vod& vod);
    void   watchDirectUrl(const std::string& url, const std::string& title,
                          const std::string& chatChannel);
    void   stopWatching();
    void   applyQuality(const std::string& quality);

    // Watches for playback dying on its own (Twitch ends the stitched stream
    // after an ad break) and transparently re-resolves the live playlist.
    void   updatePlaybackWatchdog();
    void   reconnectLive();

    // Builds a category list out of live channels, used when Helix's top-games
    // call comes back empty or fails.
    static std::vector<twitch::Game> categoriesFromStreams(
        const std::vector<twitch::Stream>& streams);

    // Resolves the broadcaster id for a channel and pulls everything that
    // hangs off it: chat badge images, room settings and channel metadata.
    void   loadChannelExtras(const std::string& login);
    // "<set>/<version>" -> image URL, or "" when unknown.
    std::string badgeUrl(const std::string& setId, const std::string& version) const;
    // URL of a 7TV/BTTV/FFZ emote matching `word`, or "" when it is plain text.
    std::string thirdPartyEmoteUrl(const std::string& word) const;

    void   refreshBrowse();
    void   openCategory(const twitch::Game& game);
    void   runSearch(const std::string& query);

    // --- player backend -----------------------------------------------------
    // VLC is the default and runs in-process; mpv stays available as a fallback
    // for people who prefer it. These wrappers hide which one is active.
    // `maxHeight` caps the rendition when `url` is a master playlist; pass 0
    // when handing over a single variant or a plain media file.
    bool   playerStart(const std::string& url, const std::string& title, bool live,
                       int maxHeight = 0);
    void   playerStop();
    bool   playerPlaying() const;
    void   playerSetBounds(int x, int y, int width, int height);
    void   playerSetVisible(bool visible);
    // Rectangle (screen coordinates) the video must not cover, so popups drawn
    // over the player stay visible. Zero size clears it.
    void   playerSetClipExclusion(int x, int y, int width, int height);
    // Registers a screen rect that must stay visible over the video. Several
    // overlays can register in one frame (control bar plus an open menu); the
    // rects are merged so one does not erase the other.
    void   registerVideoOverlay(float x, float y, float width, float height);
    void   playerSetVolume(int volume);
    bool   playerAvailable() const;
    std::string playerMissingHint() const;

    void   downloadVod(const twitch::Vod& vod);
    void   downloadPlaylist(const std::string& playlistUrl, const std::string& title);

    void*  nativeWindowHandle() const;

    // --- window / imgui ------------------------------------------------------
    GLFWwindow*   m_window = nullptr;
    WindowChrome  m_chrome;
    ui::FontSet   m_fonts;
    bool          m_fontsDirty = false;
    bool          m_running    = false;

    // --- navigation ----------------------------------------------------------
    Screen m_screen     = Screen::Following;
    Screen m_prevScreen = Screen::Following;

    // --- async plumbing ------------------------------------------------------
    std::mutex                          m_taskMutex;
    std::vector<std::function<void()>>  m_tasks;
    std::atomic<int>                    m_pendingJobs{0};

    // --- auth ----------------------------------------------------------------
    twitch::Auth m_auth;
    bool         m_loginInProgress = false;
    std::string  m_loginError;

    // --- following -----------------------------------------------------------
    std::vector<twitch::FollowedChannel> m_followed;
    std::vector<twitch::Stream>          m_liveStreams;
    bool  m_followingLoading = false;
    char  m_followFilter[128]{};
    double m_lastFollowRefresh = 0.0;

    // --- browse --------------------------------------------------------------
    std::vector<twitch::Game>   m_topGames;        // "Categories" tab
    std::vector<twitch::Stream> m_topStreams;      // "Channels" tab
    std::vector<twitch::Game>   m_searchGames;     // category hits for a query
    std::vector<twitch::Stream> m_searchStreams;   // channel hits for a query
    std::vector<twitch::Stream> m_categoryStreams; // live streams in one category
    twitch::Game m_openCategory;                   // empty id => not drilled in
    char  m_browseQuery[128]{};
    bool  m_browseLoading   = false;
    bool  m_categoryLoading = false;
    bool  m_browseLoaded    = false;   // top lists fetched at least once
    bool  m_searchActive    = false;
    int   m_browseTab       = 0;       // 0 = categories, 1 = channels
    // ImGui keeps scroll position per child-window id, and these lists reuse
    // theirs. Without an explicit reset, opening a category lands you at the
    // offset left over from the previous one.
    bool  m_categoryScrollReset = false;
    bool  m_browseScrollReset   = false;

    // --- vods ----------------------------------------------------------------
    std::vector<twitch::Vod> m_vods;
    char  m_vodChannel[64]{};
    bool  m_vodsLoading   = false;
    int   m_vodTypeIndex  = 0;   // archive / highlight / upload / all
    std::string m_vodChannelLoaded;

    // Clips share the VODs screen through a tab.
    std::vector<twitch::Clip> m_clips;
    bool  m_clipsLoading   = false;
    int   m_clipRangeIndex = 1;   // 0 = 24h, 1 = 7 days, 2 = 30 days, 3 = all time
    std::string m_clipsChannelLoaded;

    // --- recovery ------------------------------------------------------------
    char  m_recUrl[512]{};
    char  m_recStreamer[64]{};
    char  m_recStreamId[32]{};
    char  m_recStartTime[32]{};
    int   m_recWindow = 60;
    bool  m_recScanning = false;
    std::atomic<bool> m_recCancel{false};
    vod::ScanProgress m_recProgress;
    std::vector<vod::RecoveredPlaylist> m_recResults;
    std::string m_recMessage;
    std::string m_recSelectedUrl;
    vod::MutedInfo m_recMuted;

    // --- watch ---------------------------------------------------------------
    player::VlcPlayer     m_vlc;      // default, in-process
    player::MpvHost       m_player;   // optional external backend
    player::PlaybackQueue m_playback; // keeps slow start/stop off the UI thread
    chat::ChatClient      m_chat;

    // Bumped on every watch request. Async playlist results and queued player
    // work compare against it and drop themselves when the user moved on.
    std::atomic<uint64_t> m_watchGeneration{0};
    twitch::PlaylistResult m_playlist;
    std::string m_watchTitle;
    std::string m_watchChannel;      // chat channel login ("" = no chat)
    std::string m_watchSubtitle;
    bool        m_watchLoading = false;
    bool        m_watchIsLive  = false;

    // --- playback watchdog ---------------------------------------------------
    double      m_lastPlayStart     = 0.0;   // grace period before judging health
    double      m_nextRetryAt       = 0.0;
    int         m_watchRetries      = 0;
    int         m_shortSessions     = 0;     // consecutive streams that died early
    bool        m_watchReconnecting = false;
    bool        m_watchGaveUp       = false;
    // Every playback token Twitch issues carries server_ads=true, so each
    // reconnect begins with a fresh pre-roll. If reconnects keep dying before
    // the ad is over, retrying only replays adverts forever.
    bool        m_watchAdLoop       = false;
    char        m_chatInput[512]{};
    bool        m_chatScrollToBottom = true;
    // Snapshot of the chat log, refreshed only when the client reports a new
    // version. Copying the whole buffer every frame was the main source of
    // stutter on busy channels.
    std::vector<chat::Message> m_chatCache;
    uint64_t    m_chatCacheVersion = UINT64_MAX;

    // Chat presentation and channel metadata for the room being watched.
    std::unordered_map<std::string, std::string> m_badgeUrls;
    // Global sets are fetched once per session; channel sets are merged on top
    // and replaced whenever the room changes.
    chat::EmoteTable m_globalEmotes;
    chat::EmoteTable m_roomEmotes;
    bool             m_globalEmotesLoaded = false;
    twitch::ChatSettings m_chatSettings;
    twitch::ChannelInfo  m_channelInfo;
    bool        m_chatSettingsValid = false;
    bool        m_channelInfoValid  = false;
    int         m_playerVolume = 80;
    // Fullscreen control bar auto-hides while the pointer sits still.
    double      m_lastPointerMove = 0.0;

    // Screen rect of any overlay drawn on top of the player (quality menu,
    // modal dialogs) that the video's native child window must not cover.
    // x, y, width, height; width 0 means nothing to exclude.
    //
    // Overlays register into `Next` while the frame is being built; it is
    // swapped into the applied rect at the end of the frame. That keeps a
    // single owner instead of each overlay clearing the others' value.
    float       m_videoOverlay[4]{0, 0, 0, 0};
    float       m_videoOverlayNext[4]{0, 0, 0, 0};

    // --- settings ------------------------------------------------------------
    char m_setMpvPath[512]{};
    char m_setFfmpegPath[512]{};
    char m_setDownloadDir[512]{};
    char m_setClientId[128]{};
    bool m_settingsDirty = false;

    // --- modals / toasts ------------------------------------------------------
    std::vector<Toast> m_toasts;
    bool m_showLogoutConfirm = false;
};

} // namespace dfts
