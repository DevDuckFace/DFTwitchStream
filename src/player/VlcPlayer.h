#pragma once
#include "player/VideoSurface.h"

#include <string>
#include <vector>
#include <mutex>
#include <atomic>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

namespace dfts::player {

// In-process video playback through libVLC, rendered into a child window that
// sits over the OpenGL surface. Nothing is spawned as a separate process, so
// the picture is genuinely part of the application window.
class VlcPlayer {
public:
    enum class State { Idle, Opening, Buffering, Playing, Paused, Stopped, Ended, Error };

    struct Options {
        std::string title;
        std::string httpReferrer = "https://www.twitch.tv/";
        std::string userAgent    = "Mozilla/5.0 (Windows NT 10.0; Win64; x64)";
        bool        lowLatency   = true;
        int         volume       = 80;
        int         networkCacheMs = 0;   // 0 => derived from `lowLatency`
        // When playing a master playlist, caps the rendition VLC may select.
        // 0 leaves the choice to VLC (or means the URL is already a variant).
        int         maxHeight    = 0;
    };

    VlcPlayer() = default;
    ~VlcPlayer();

    VlcPlayer(const VlcPlayer&)            = delete;
    VlcPlayer& operator=(const VlcPlayer&) = delete;

    // Creates the libVLC instance and the video surface. Safe to call twice.
    bool attach(void* parentHwnd);
    void detach();

    bool available() const { return m_instance != nullptr; }

    // Plays a URL (HLS playlist, direct media) or a local file path.
    bool play(const std::string& url, const Options& opts);
    void stop();
    void setPaused(bool paused);
    void togglePause();

    bool  playing() const;
    State state() const { return m_state.load(); }

    // --- transport ----------------------------------------------------------
    void   setVolume(int volume);          // 0..200
    int    volume() const { return m_volume; }
    void   seekRelative(double seconds);
    void   setPosition(float fraction);    // 0..1
    float  position() const;               // 0..1, -1 when unknown
    int64_t timeMs() const;                // -1 when unknown
    int64_t lengthMs() const;              // 0 for live streams
    bool   seekable() const;

    // --- surface ------------------------------------------------------------
    void setBounds(int x, int y, int width, int height) { m_surface.setBounds(x, y, width, height); }
    void setVisible(bool visible)                       { m_surface.setVisible(visible); }
    // Lets UI drawn over the video (menus, overlays) show through.
    void setClipExclusion(int x, int y, int width, int height) {
        m_surface.setClipExclusion(x, y, width, height);
    }

    const std::string& lastError()  const { return m_lastError; }
    const std::string& currentUrl() const { return m_currentUrl; }

    // Human-readable libVLC version, for the About screen.
    static std::string versionString();

private:
    void releaseMedia();
    void hookEvents();

    libvlc_instance_t*     m_instance = nullptr;
    libvlc_media_player_t* m_player   = nullptr;
    libvlc_media_t*        m_media    = nullptr;

    VideoSurface        m_surface;
    std::atomic<State>  m_state{State::Idle};
    std::string         m_lastError;
    std::string         m_currentUrl;
    int                 m_volume = 80;
};

} // namespace dfts::player
