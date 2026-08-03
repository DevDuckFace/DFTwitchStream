#include "player/VlcPlayer.h"
#include "core/Util.h"
#include "core/Log.h"

#include <vlc/vlc.h>

#include <algorithm>

namespace dfts::player {

namespace {

// Maps libVLC's event ids onto our coarse state enum.
void onVlcEvent(const libvlc_event_t* event, void* userData) {
    auto* state = static_cast<std::atomic<VlcPlayer::State>*>(userData);
    switch (event->type) {
        case libvlc_MediaPlayerOpening:      state->store(VlcPlayer::State::Opening);   break;
        case libvlc_MediaPlayerBuffering:    state->store(VlcPlayer::State::Buffering); break;
        case libvlc_MediaPlayerPlaying:      state->store(VlcPlayer::State::Playing);   break;
        case libvlc_MediaPlayerPaused:       state->store(VlcPlayer::State::Paused);    break;
        case libvlc_MediaPlayerStopped:      state->store(VlcPlayer::State::Stopped);   break;
        case libvlc_MediaPlayerEndReached:   state->store(VlcPlayer::State::Ended);     break;
        case libvlc_MediaPlayerEncounteredError: state->store(VlcPlayer::State::Error); break;
        default: break;
    }
}

} // namespace

VlcPlayer::~VlcPlayer() { detach(); }

std::string VlcPlayer::versionString() {
    const char* version = libvlc_get_version();
    return version ? version : "unknown";
}

bool VlcPlayer::attach(void* parentHwnd) {
    if (!m_surface.create(parentHwnd)) return false;
    if (m_instance) return true;

    // libVLC looks for its codec plugins next to the exe; the build stages them
    // into ./plugins, so point it there explicitly rather than relying on the
    // registry entries a full VLC installation would have written.
    static std::string pluginPath = "--plugin-path=" + util::exeDir() + "\\plugins";

    std::vector<const char*> args = {
        "--intf=dummy",
        "--no-video-title-show",
        "--no-osd",
        "--no-snapshot-preview",
        "--no-stats",
        "--no-sub-autodetect-file",
        "--quiet",
        "--no-xlib",
        pluginPath.c_str(),
    };

    m_instance = libvlc_new((int)args.size(), args.data());
    if (!m_instance) {
        m_lastError = "libvlc_new failed (plugins missing?)";
        LOG_E(m_lastError);
        return false;
    }

    m_player = libvlc_media_player_new(m_instance);
    if (!m_player) {
        m_lastError = "libvlc_media_player_new failed";
        LOG_E(m_lastError);
        libvlc_release(m_instance);
        m_instance = nullptr;
        return false;
    }

    // Render into our child window rather than a window of VLC's own.
    libvlc_media_player_set_hwnd(m_player, m_surface.handle());
    libvlc_video_set_mouse_input(m_player, 0);
    libvlc_video_set_key_input(m_player, 0);
    libvlc_audio_set_volume(m_player, m_volume);

    hookEvents();
    LOG_I("libVLC " + versionString() + " initialised");
    return true;
}

void VlcPlayer::hookEvents() {
    libvlc_event_manager_t* events = libvlc_media_player_event_manager(m_player);
    if (!events) return;
    const libvlc_event_e wanted[] = {
        libvlc_MediaPlayerOpening,   libvlc_MediaPlayerBuffering,
        libvlc_MediaPlayerPlaying,   libvlc_MediaPlayerPaused,
        libvlc_MediaPlayerStopped,   libvlc_MediaPlayerEndReached,
        libvlc_MediaPlayerEncounteredError,
    };
    for (libvlc_event_e e : wanted)
        libvlc_event_attach(events, e, onVlcEvent, &m_state);
}

void VlcPlayer::detach() {
    stop();
    if (m_player) {
        libvlc_media_player_release(m_player);
        m_player = nullptr;
    }
    if (m_instance) {
        libvlc_release(m_instance);
        m_instance = nullptr;
    }
    m_surface.destroy();
}

void VlcPlayer::releaseMedia() {
    if (m_media) {
        libvlc_media_release(m_media);
        m_media = nullptr;
    }
}

bool VlcPlayer::play(const std::string& url, const Options& opts) {
    if (!m_instance || !m_player) {
        m_lastError = "libVLC is not initialised";
        return false;
    }

    libvlc_media_player_stop(m_player);
    releaseMedia();

    m_media = libvlc_media_new_location(m_instance, url.c_str());
    if (!m_media) {
        m_lastError = "cannot open " + url;
        LOG_E(m_lastError);
        m_state = State::Error;
        return false;
    }

    // Twitch's CDN rejects requests without a browser-shaped Referer/UA pair.
    libvlc_media_add_option(m_media, (":http-referrer=" + opts.httpReferrer).c_str());
    libvlc_media_add_option(m_media, (":http-user-agent=" + opts.userAgent).c_str());
    libvlc_media_add_option(m_media, ":http-reconnect");

    // A short network cache keeps live playback near the broadcast edge; VODs
    // benefit from a larger one because seeking refills it.
    int cacheMs = opts.networkCacheMs > 0 ? opts.networkCacheMs
                                          : (opts.lowLatency ? 1200 : 4000);
    libvlc_media_add_option(m_media, (":network-caching=" + std::to_string(cacheMs)).c_str());
    libvlc_media_add_option(m_media, ":adaptive-livedelay=2000");
    if (opts.lowLatency) libvlc_media_add_option(m_media, ":clock-jitter=0");

    // Playing the master playlist lets VLC's adaptive demuxer own the stream,
    // which is what survives Twitch swapping playlists at the end of an advert.
    // The quality the user picked becomes a ceiling instead of a fixed URL.
    if (opts.maxHeight > 0) {
        libvlc_media_add_option(m_media,
                                (":adaptive-maxheight=" + std::to_string(opts.maxHeight)).c_str());
        libvlc_media_add_option(m_media, ":adaptive-logic=highest");
    }

    libvlc_media_player_set_media(m_player, m_media);
    libvlc_media_player_set_hwnd(m_player, m_surface.handle());

    m_volume = std::clamp(opts.volume, 0, 200);
    libvlc_audio_set_volume(m_player, m_volume);

    if (libvlc_media_player_play(m_player) != 0) {
        m_lastError = "libvlc_media_player_play failed";
        LOG_E(m_lastError);
        m_state = State::Error;
        return false;
    }

    m_currentUrl = url;
    m_lastError.clear();
    m_state = State::Opening;
    // Visibility is driven by the UI thread each frame. Calling ShowWindow from
    // the playback worker would cross into the window's owning thread and can
    // deadlock if that thread is busy.
    LOG_I("libVLC playing " + url);
    return true;
}

void VlcPlayer::stop() {
    // Blocking: libVLC waits for the decoder to unwind. Callers must run this
    // off the UI thread (see PlaybackQueue).
    if (m_player) libvlc_media_player_stop(m_player);
    releaseMedia();
    m_currentUrl.clear();
    m_state = State::Idle;
}

bool VlcPlayer::playing() const {
    if (!m_player) return false;
    return libvlc_media_player_is_playing(m_player) != 0;
}

void VlcPlayer::setPaused(bool paused) {
    if (m_player) libvlc_media_player_set_pause(m_player, paused ? 1 : 0);
}

void VlcPlayer::togglePause() {
    if (m_player) libvlc_media_player_pause(m_player);
}

void VlcPlayer::setVolume(int volume) {
    m_volume = std::clamp(volume, 0, 200);
    if (m_player) libvlc_audio_set_volume(m_player, m_volume);
}

void VlcPlayer::seekRelative(double seconds) {
    if (!m_player || !seekable()) return;
    libvlc_time_t now = libvlc_media_player_get_time(m_player);
    if (now < 0) return;
    libvlc_time_t target = now + (libvlc_time_t)(seconds * 1000.0);
    libvlc_media_player_set_time(m_player, std::max<libvlc_time_t>(0, target));
}

void VlcPlayer::setPosition(float fraction) {
    if (!m_player || !seekable()) return;
    libvlc_media_player_set_position(m_player, std::clamp(fraction, 0.0f, 1.0f));
}

float VlcPlayer::position() const {
    if (!m_player) return -1.0f;
    return libvlc_media_player_get_position(m_player);
}

int64_t VlcPlayer::timeMs() const {
    if (!m_player) return -1;
    return (int64_t)libvlc_media_player_get_time(m_player);
}

int64_t VlcPlayer::lengthMs() const {
    if (!m_player) return 0;
    libvlc_time_t length = libvlc_media_player_get_length(m_player);
    return length > 0 ? (int64_t)length : 0;
}

bool VlcPlayer::seekable() const {
    return m_player && libvlc_media_player_is_seekable(m_player) != 0;
}

} // namespace dfts::player
