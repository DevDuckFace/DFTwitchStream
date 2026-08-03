#include "player/MpvHost.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>

#include <algorithm>

namespace dfts::player {

namespace {

constexpr const wchar_t* kChildClass = L"DFTS_VideoSurface";

// The child window only has to exist and stay black; mpv paints over it.
LRESULT CALLBACK videoSurfaceProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC dc = (HDC)wp;
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(12, 12, 16));
            FillRect(dc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        // Forward input to the parent so ImGui keeps receiving keyboard shortcuts.
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            HWND parent = GetParent(hwnd);
            if (parent) return SendMessageW(parent, msg, wp, lp);
            break;
        }
        default: break;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void registerClassOnce() {
    static bool registered = false;
    if (registered) return;
    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = videoSurfaceProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = kChildClass;
    RegisterClassExW(&wc);
    registered = true;
}

// Writes one line to mpv's JSON IPC named pipe. Best effort: mpv may not have
// created the pipe yet during the first moments after launch.
bool writePipeLine(const std::string& pipeName, const std::string& line) {
    if (pipeName.empty()) return false;
    std::wstring path = util::widen("\\\\.\\pipe\\" + pipeName);

    HANDLE pipe = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, 0, nullptr);
    if (pipe == INVALID_HANDLE_VALUE) return false;

    std::string payload = line + "\n";
    DWORD written = 0;
    BOOL ok = WriteFile(pipe, payload.data(), (DWORD)payload.size(), &written, nullptr);
    CloseHandle(pipe);
    return ok && written == payload.size();
}

} // namespace

MpvHost::~MpvHost() {
    stop();
    detach();
}

std::string MpvHost::detectMpv(const std::string& configuredPath) {
    if (!configuredPath.empty() && util::fileExists(configuredPath)) return configuredPath;
    std::string found = util::findOnPath("mpv");
    if (!found.empty()) return found;
    // mpv is commonly unpacked next to the app by the installer.
    std::string bundled = util::exeDir() + "\\mpv\\mpv.exe";
    if (util::fileExists(bundled)) return bundled;
    return {};
}

bool MpvHost::attach(void* parentHwnd) {
    m_parent = parentHwnd;
    return ensureChildWindow();
}

bool MpvHost::ensureChildWindow() {
    if (m_child) return true;
    if (!m_parent) return false;

    registerClassOnce();
    HWND child = CreateWindowExW(
        0, kChildClass, L"", WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
        0, 0, 16, 16, (HWND)m_parent, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!child) {
        m_lastError = "cannot create video surface";
        LOG_E(m_lastError);
        return false;
    }
    m_child = child;
    return true;
}

void MpvHost::detach() {
    if (m_child) {
        DestroyWindow((HWND)m_child);
        m_child = nullptr;
    }
    m_parent = nullptr;
}

void MpvHost::setBounds(int x, int y, int width, int height) {
    if (!m_child) return;
    width  = std::max(1, width);
    height = std::max(1, height);
    SetWindowPos((HWND)m_child, HWND_TOP, x, y, width, height,
                 SWP_NOACTIVATE | (m_visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
}

void MpvHost::setVisible(bool visible) {
    if (m_visible == visible) return;
    m_visible = visible;
    if (m_child) ShowWindow((HWND)m_child, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void MpvHost::setClipExclusion(int x, int y, int width, int height) {
    if (!m_child) return;
    if (x == m_clipX && y == m_clipY && width == m_clipW && height == m_clipH) return;
    m_clipX = x; m_clipY = y; m_clipW = width; m_clipH = height;

    if (width <= 0 || height <= 0) {
        SetWindowRgn((HWND)m_child, nullptr, TRUE);
        return;
    }

    RECT client{};
    GetClientRect((HWND)m_child, &client);
    HRGN full = CreateRectRgn(client.left, client.top, client.right, client.bottom);
    HRGN hole = CreateRectRgn(x, y, x + width, y + height);
    CombineRgn(full, full, hole, RGN_DIFF);
    DeleteObject(hole);
    SetWindowRgn((HWND)m_child, full, TRUE);   // takes ownership of `full`
}

std::vector<std::string> MpvHost::baseArgs(const Options& opts) const {
    std::vector<std::string> args = {
        "--wid=" + std::to_string((uintptr_t)m_child),
        "--input-ipc-server=\\\\.\\pipe\\" + m_pipeName,
        "--no-border",
        "--keep-open=no",
        "--idle=no",
        "--osc=yes",
        "--osd-level=1",
        "--force-window=yes",
        "--hwdec=auto-safe",
        "--volume=" + std::to_string(std::clamp(opts.volume, 0, 130)),
        "--http-header-fields=Referer: " + opts.httpReferrer,
        "--user-agent=Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
    };
    if (!opts.title.empty()) args.push_back("--force-media-title=" + opts.title);

    if (opts.lowLatency) {
        // Small demuxer window keeps live playback close to the broadcast edge.
        args.push_back("--cache=yes");
        args.push_back("--demuxer-max-bytes=25MiB");
        args.push_back("--demuxer-readahead-secs=1.5");
        args.push_back("--cache-secs=2");
        args.push_back("--profile=low-latency");
    } else {
        args.push_back("--cache=yes");
        args.push_back("--demuxer-max-bytes=150MiB");
        args.push_back("--cache-secs=20");
    }
    return args;
}

bool MpvHost::play(const std::string& url, const Options& opts) {
    stop();
    if (!ensureChildWindow()) return false;

    std::string mpv = detectMpv(opts.mpvPath);
    if (mpv.empty()) {
        m_lastError = "mpv not found";
        LOG_E(m_lastError);
        return false;
    }

    m_pipeName = "dfts-mpv-" + util::randomHex(6);
    std::vector<std::string> args = baseArgs(opts);
    args.push_back("--");
    args.push_back(url);

    if (!m_process.start(mpv, args, /*hideWindow=*/true)) {
        m_lastError = "failed to start mpv";
        return false;
    }
    m_currentUrl = url;
    m_lastError.clear();
    // Visibility is owned by the UI thread; see the note in VlcPlayer::play.
    LOG_I("mpv started for " + url);
    return true;
}

void MpvHost::stop() {
    if (m_process.running()) {
        // Ask mpv to quit cleanly so it releases the decoder before we kill it.
        writePipeLine(m_pipeName, R"({"command":["quit"]})");
        Sleep(120);
    }
    m_process.kill();
    m_currentUrl.clear();
    m_pipeName.clear();
}

bool MpvHost::playing() const { return m_process.running(); }

void MpvHost::sendCommand(const std::string& jsonCommandArray) {
    writePipeLine(m_pipeName, R"({"command":)" + jsonCommandArray + "}");
}

void MpvHost::setVolume(int volume) {
    sendCommand(R"(["set_property","volume",)" + std::to_string(std::clamp(volume, 0, 130)) + "]");
}

void MpvHost::setPaused(bool paused) {
    sendCommand(std::string(R"(["set_property","pause",)") + (paused ? "true" : "false") + "]");
}

void MpvHost::togglePause() {
    sendCommand(R"(["cycle","pause"])");
}

void MpvHost::seek(double seconds) {
    sendCommand(R"(["seek",)" + std::to_string(seconds) + R"(,"relative"])");
}

} // namespace dfts::player
