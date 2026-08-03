#pragma once
#include <string>
#include <vector>
#include <memory>

#include "core/Process.h"

namespace dfts::player {

// Hosts mpv inside a borderless child window layered over the application's
// OpenGL surface. mpv renders into the child HWND via its --wid option, so the
// video appears as part of the UI instead of a separate top-level window.
class MpvHost {
public:
    struct Options {
        std::string mpvPath;          // "" => auto-detect on PATH
        bool        lowLatency    = true;
        int         volume        = 80;
        std::string title;            // shown in mpv's OSD
        std::string httpReferrer = "https://www.twitch.tv/";
    };

    MpvHost() = default;
    ~MpvHost();

    MpvHost(const MpvHost&)            = delete;
    MpvHost& operator=(const MpvHost&) = delete;

    // Creates the child window. `parentHwnd` is the GLFW window handle.
    bool attach(void* parentHwnd);
    void detach();

    // Plays a direct media URL (an HLS variant playlist, a VOD playlist, or a
    // local file). Restarts mpv if it is already running.
    bool play(const std::string& url, const Options& opts);

    void stop();
    bool playing() const;

    // Positions the video surface, in pixels relative to the parent client area.
    void setBounds(int x, int y, int width, int height);
    void setVisible(bool visible);
    // See VlcPlayer::setClipExclusion; keeps both backends behaving alike.
    void setClipExclusion(int x, int y, int width, int height);

    // Runtime control through mpv's JSON IPC pipe. No-ops when mpv is not running.
    void setVolume(int volume);
    void setPaused(bool paused);
    void togglePause();
    void seek(double seconds);
    void sendCommand(const std::string& jsonCommandArray);

    const std::string& lastError() const { return m_lastError; }
    const std::string& currentUrl() const { return m_currentUrl; }

    // Resolves mpv on the system. Returns "" when it cannot be found.
    static std::string detectMpv(const std::string& configuredPath);

private:
    bool ensureChildWindow();
    std::vector<std::string> baseArgs(const Options& opts) const;

    void*       m_parent = nullptr;   // HWND
    void*       m_child  = nullptr;   // HWND
    proc::Child m_process;
    std::string m_pipeName;
    std::string m_lastError;
    std::string m_currentUrl;
    bool        m_visible = false;

    // Last applied clip exclusion, so the region is only rebuilt on change.
    int         m_clipX = 0, m_clipY = 0, m_clipW = 0, m_clipH = 0;
};

} // namespace dfts::player
