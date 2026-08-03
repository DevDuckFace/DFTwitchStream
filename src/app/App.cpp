#include "app/App.h"
#include "core/Log.h"
#include "core/Util.h"
#include "core/Http.h"
#include "i18n/I18n.h"
#include "ui/Theme.h"
#include "ui/TextureCache.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>

#include <windows.h>
#include <dwmapi.h>

#include <algorithm>
#include <set>
#include <thread>
#include <unordered_map>

namespace dfts {

namespace {

void glfwErrorCallback(int code, const char* description) {
    LOG_E("glfw error " + std::to_string(code) + ": " + (description ? description : ""));
}

// Windows 10 2004+ honours this to darken the title bar and window border.
void applyDarkTitleBar(HWND hwnd) {
    BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
}

// Height of the custom title bar that replaces the Windows caption.
constexpr int kTitleBarHeight = 38;

struct NavItem {
    Screen      screen;
    const char* key;
    ui::Icon    icon;
};

const NavItem kNavItems[] = {
    {Screen::Following, "nav.following", ui::Icon::Following},
    {Screen::Browse,    "nav.browse",    ui::Icon::Browse},
    {Screen::Vods,      "nav.vods",      ui::Icon::Vods},
    {Screen::Recovery,  "nav.recovery",  ui::Icon::Recovery},
    {Screen::Downloads, "nav.downloads", ui::Icon::Downloads},
    {Screen::Watch,     "nav.watch",     ui::Icon::Watching},
};

} // namespace

App::App() = default;

App::~App() { shutdown(); }

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------
bool App::init() {
    Config::get().load();
    Settings& s = Config::get().settings();
    I18n::get().setLanguage(s.language);

    glfwSetErrorCallback(glfwErrorCallback);
    if (!glfwInit()) {
        LOG_E("glfwInit failed");
        return false;
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);

    m_window = glfwCreateWindow(1440, 900, "DF Twitch Stream", nullptr, nullptr);
    if (!m_window) {
        LOG_E("cannot create window");
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1);
    glfwSetWindowSizeLimits(m_window, 1024, 640, GLFW_DONT_CARE, GLFW_DONT_CARE);
    if (s.startMaximized) glfwMaximizeWindow(m_window);

    HWND hwnd = glfwGetWin32Window(m_window);
    applyDarkTitleBar(hwnd);

    // Attach the app icon explicitly. GLFW falls back to the generic Windows
    // application icon unless it finds one, and the task bar reads the large
    // icon from the window rather than from the executable.
    HINSTANCE instance = GetModuleHandleW(nullptr);
    if (HICON bigIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                          GetSystemMetrics(SM_CXICON),
                                          GetSystemMetrics(SM_CYICON), 0))
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, (LPARAM)bigIcon);
    if (HICON smallIcon = (HICON)LoadImageW(instance, MAKEINTRESOURCEW(1), IMAGE_ICON,
                                            GetSystemMetrics(SM_CXSMICON),
                                            GetSystemMetrics(SM_CYSMICON), 0))
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon);

    // Replace the Windows caption with our own, keeping the native resize
    // frame, snapping and shadow.
    m_chrome.setCaptionHeight(kTitleBarHeight);
    m_chrome.install(hwnd);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.IniFilename = nullptr;   // window layout is fixed; nothing to persist

    ui::applyTheme();
    m_fonts = ui::loadFonts(s.uiScale);

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 330");

    // libVLC is the default backend and lives in-process; mpv is only attached
    // so the fallback path can be switched on without restarting.
    if (!m_vlc.attach(hwnd))
        LOG_W("libVLC unavailable: " + m_vlc.lastError());
    m_player.attach(hwnd);
    m_playerVolume = s.volume;

    // All player start/stop work runs here, never on the render thread.
    m_playback.start([this](const player::PlaybackQueue::Request& request) {
        if (request.stop) {
            m_vlc.stop();
            m_player.stop();
            return;
        }
        // The user may have switched again while this request waited.
        if (request.generation < m_playback.currentGeneration()) return;

        const Settings& settings = Config::get().settings();
        bool ok = false;

        if (settings.playerBackend == 0) {
            m_player.stop();
            player::VlcPlayer::Options opts;
            opts.title      = request.title;
            opts.lowLatency = request.live && settings.lowLatency;
            opts.volume     = m_playerVolume;
            opts.maxHeight  = request.maxHeight;
            ok = m_vlc.play(request.url, opts);
        } else {
            m_vlc.stop();
            player::MpvHost::Options opts;
            opts.mpvPath    = settings.mpvPath;
            opts.lowLatency = request.live && settings.lowLatency;
            opts.volume     = m_playerVolume;
            opts.title      = request.title;
            ok = m_player.play(request.url, opts);
        }

        if (!ok) {
            std::string error = settings.playerBackend == 0 ? m_vlc.lastError()
                                                            : m_player.lastError();
            post([this, error]() {
                toastError(error.empty() ? playerMissingHint() : error);
            });
        }
    });

    // Seed the settings text buffers from the stored configuration.
    auto copyInto = [](char* dst, size_t n, const std::string& src) {
        std::snprintf(dst, n, "%s", src.c_str());
    };
    copyInto(m_setMpvPath,        sizeof(m_setMpvPath),        s.mpvPath);
    copyInto(m_setFfmpegPath,     sizeof(m_setFfmpegPath),     s.ffmpegPath);
    copyInto(m_setDownloadDir,    sizeof(m_setDownloadDir),    s.downloadDir);
    copyInto(m_setClientId,       sizeof(m_setClientId),       s.clientId);

    m_screen = s.loggedIn() ? Screen::Following : Screen::Login;
    if (s.loggedIn()) restoreSession();

    LOG_I("DFTwitchStream " DFTS_VERSION " started");
    m_running = true;
    return true;
}

void App::shutdown() {
    if (!m_window) return;

    m_chrome.uninstall();
    m_recCancel = true;
    m_auth.cancel();
    m_chat.leave();
    m_playback.shutdown();   // must finish before the players are torn down
    m_vlc.stop();
    m_vlc.detach();
    m_player.stop();
    m_player.detach();
    ui::TextureCache::get().clear();
    vod::DownloadManager::get().cancelAll();

    Config::get().save();

    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(m_window);
    glfwTerminate();
    m_window = nullptr;
    LOG_I("shut down cleanly");
}

int App::run() {
    while (m_running && !glfwWindowShouldClose(m_window)) {
        glfwPollEvents();

        // Sleeping while minimised keeps the app off the CPU entirely.
        if (glfwGetWindowAttrib(m_window, GLFW_ICONIFIED)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(60));
            continue;
        }

        drainTasks();
        // GL uploads have to happen on the thread that owns the context.
        ui::TextureCache::get().processCompleted();
        updatePlaybackWatchdog();

        if (m_fontsDirty) {
            ImGui_ImplOpenGL3_DestroyFontsTexture();
            m_fonts = ui::loadFonts(Config::get().settings().uiScale);
            ImGui_ImplOpenGL3_CreateFontsTexture();
            m_fontsDirty = false;
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        drawFrame();

        ImGui::Render();
        int w = 0, h = 0;
        glfwGetFramebufferSize(m_window, &w, &h);
        glViewport(0, 0, w, h);
        ImVec4 bg = ImGui::ColorConvertU32ToFloat4(ui::col::kBg);
        glClearColor(bg.x, bg.y, bg.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Async plumbing
// ---------------------------------------------------------------------------
void App::runAsync(std::function<void()> work) {
    ++m_pendingJobs;
    std::thread([this, work = std::move(work)]() {
        try {
            work();
        } catch (const std::exception& e) {
            LOG_E(std::string("background task threw: ") + e.what());
        }
        --m_pendingJobs;
    }).detach();
}

void App::post(std::function<void()> fn) {
    std::lock_guard<std::mutex> lock(m_taskMutex);
    m_tasks.push_back(std::move(fn));
}

void App::drainTasks() {
    std::vector<std::function<void()>> tasks;
    {
        std::lock_guard<std::mutex> lock(m_taskMutex);
        tasks.swap(m_tasks);
    }
    for (auto& t : tasks) t();
}

twitch::Api App::makeApi() const {
    const Settings& s = Config::get().settings();
    return twitch::Api(Config::get().effectiveClientId(), s.accessToken);
}

bool App::requireLogin() {
    if (Config::get().settings().loggedIn()) return true;
    setScreen(Screen::Login);
    return false;
}

void* App::nativeWindowHandle() const {
    return m_window ? (void*)glfwGetWin32Window(m_window) : nullptr;
}

// ---------------------------------------------------------------------------
// Navigation / notifications
// ---------------------------------------------------------------------------
void App::setScreen(Screen s) {
    if (m_screen == s) return;
    m_prevScreen = m_screen;
    m_screen     = s;
    // The video surface is a native child window, so it must be hidden whenever
    // it is not the screen being shown or it would cover the UI.
    playerSetVisible(s == Screen::Watch && playerPlaying());
}

// ---------------------------------------------------------------------------
// Player backend
// ---------------------------------------------------------------------------
bool App::playerAvailable() const {
    if (Config::get().settings().playerBackend == 0) return m_vlc.available();
    return !player::MpvHost::detectMpv(Config::get().settings().mpvPath).empty();
}

std::string App::playerMissingHint() const {
    if (Config::get().settings().playerBackend == 0)
        return m_vlc.lastError().empty() ? T("watch.vlcMissing") : m_vlc.lastError();
    return T("watch.mpvMissing");
}

bool App::playerStart(const std::string& url, const std::string& title, bool live,
                      int maxHeight) {
    // Returns immediately; the worker does the blocking part and reports any
    // failure through a toast.
    player::PlaybackQueue::Request request;
    request.url       = url;
    request.title     = title;
    request.live      = live;
    request.maxHeight = maxHeight;
    m_playback.submit(request);

    // Give the stream time to open before the watchdog judges its health.
    m_lastPlayStart = ImGui::GetTime();
    return true;
}

void App::playerStop() {
    player::PlaybackQueue::Request request;
    request.stop = true;
    m_playback.submit(request);
}

bool App::playerPlaying() const {
    return Config::get().settings().playerBackend == 0 ? m_vlc.playing() : m_player.playing();
}

void App::playerSetBounds(int x, int y, int width, int height) {
    if (Config::get().settings().playerBackend == 0) m_vlc.setBounds(x, y, width, height);
    else                                             m_player.setBounds(x, y, width, height);
}

void App::playerSetVisible(bool visible) {
    // Whichever backend is idle must stay hidden regardless.
    if (Config::get().settings().playerBackend == 0) {
        m_vlc.setVisible(visible);
        m_player.setVisible(false);
    } else {
        m_player.setVisible(visible);
        m_vlc.setVisible(false);
    }
}

void App::playerSetVolume(int volume) {
    m_vlc.setVolume(volume);
    m_player.setVolume(volume);
}

void App::registerVideoOverlay(float x, float y, float width, float height) {
    if (width <= 0.0f || height <= 0.0f) return;

    if (m_videoOverlayNext[2] <= 0.0f) {
        m_videoOverlayNext[0] = x;
        m_videoOverlayNext[1] = y;
        m_videoOverlayNext[2] = width;
        m_videoOverlayNext[3] = height;
        return;
    }

    // Merge with what is already registered this frame.
    const float left   = std::min(m_videoOverlayNext[0], x);
    const float top    = std::min(m_videoOverlayNext[1], y);
    const float right  = std::max(m_videoOverlayNext[0] + m_videoOverlayNext[2], x + width);
    const float bottom = std::max(m_videoOverlayNext[1] + m_videoOverlayNext[3], y + height);

    m_videoOverlayNext[0] = left;
    m_videoOverlayNext[1] = top;
    m_videoOverlayNext[2] = right - left;
    m_videoOverlayNext[3] = bottom - top;
}

void App::playerSetClipExclusion(int x, int y, int width, int height) {
    if (Config::get().settings().playerBackend == 0)
        m_vlc.setClipExclusion(x, y, width, height);
    else
        m_player.setClipExclusion(x, y, width, height);
}

void App::toast(const std::string& text, ImU32 color) {
    post([this, text, color]() {
        m_toasts.push_back({text, color, ImGui::GetTime() + 4.0});
        if (m_toasts.size() > 5) m_toasts.erase(m_toasts.begin());
    });
}

bool App::fullscreen() const { return m_chrome.fullscreen(); }

void App::toggleFullscreen() {
    if (!m_chrome.fullscreen() && m_screen != Screen::Watch) setScreen(Screen::Watch);
    m_chrome.toggleFullscreen();
    m_lastPointerMove = ImGui::GetTime();
}

void App::toastOk(const std::string& text)    { toast(text, ui::col::kOk); }
void App::toastError(const std::string& text) { toast(text, ui::col::kError); }

void App::applyLanguage(Language lang) {
    Config::get().settings().language = lang;
    I18n::get().setLanguage(lang);
    Config::get().save();
}

void App::rebuildFonts() { m_fontsDirty = true; }

// ---------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------
void App::drawFrame() {
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::SetNextWindowViewport(vp->ID);

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                             ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar(2);

    // Overlays re-register themselves every frame.
    m_videoOverlayNext[2] = 0.0f;
    m_videoOverlayNext[3] = 0.0f;

    // --- keyboard shortcuts -------------------------------------------------
    ImGuiIO& io = ImGui::GetIO();
    if (io.MouseDelta.x != 0.0f || io.MouseDelta.y != 0.0f)
        m_lastPointerMove = ImGui::GetTime();

    if (!io.WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_F11, false) ||
            (m_screen == Screen::Watch && ImGui::IsKeyPressed(ImGuiKey_F, false)))
            toggleFullscreen();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false) && m_chrome.fullscreen())
            toggleFullscreen();
    }

    if (m_chrome.fullscreen() && m_screen == Screen::Watch) {
        // Nothing but the player: no title bar, sidebar or top bar.
        drawWatch();
    } else {
        drawTitleBar();

        if (m_screen == Screen::Login && !Config::get().settings().loggedIn()) {
            // The login screen owns the whole window; no chrome around it.
            drawLogin();
        } else {
            drawSidebar();
            ImGui::SameLine(0, 0);

            ImGui::BeginGroup();
            drawTopBar();
            drawContent();
            ImGui::EndGroup();
        }
    }

    drawToasts();
    drawModals();

    // Publish what the overlays registered; the video pane reads it next frame.
    for (int i = 0; i < 4; ++i) m_videoOverlay[i] = m_videoOverlayNext[i];

    ImGui::End();
}

void App::drawTitleBar() {
    const float height = (float)kTitleBarHeight;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kPanel));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 0));
    ImGui::BeginChild("##titlebar", ImVec2(0, height),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    // --- duck mark + product name -----------------------------------------
    const float iconSize = 22.0f;
    ImGui::SetCursorPosY((height - iconSize) * 0.5f);
    if (ImTextureID icon = ui::TextureCache::get().textureFromFile(
            util::exeDir() + "\\assets\\duck.png")) {
        ImGui::Image(icon, ImVec2(iconSize, iconSize));
    } else {
        ImGui::Dummy(ImVec2(iconSize, iconSize));
    }

    ImGui::SameLine(0, 9);
    ImGui::SetCursorPosY((height - ImGui::GetTextLineHeight()) * 0.5f);
    ImGui::PushFont(m_fonts.medium);
    ImGui::TextUnformatted("DFTwitchStream");
    ImGui::PopFont();

    // --- window buttons ----------------------------------------------------
    // Drawn by hand rather than as text: font glyphs for minimise/maximise sit
    // on a text baseline and will not line up inside a full-height button.
    const float buttonWidth = 46.0f;
    const float startX      = ImGui::GetWindowWidth() - buttonWidth * 3.0f;

    enum class WindowButton { Minimize, Maximize, Close };
    auto windowButton = [&](WindowButton kind, int index) -> bool {
        ImGui::SetCursorPos(ImVec2(startX + buttonWidth * index, 0.0f));

        const char* ids[] = {"##btnMin", "##btnMax", "##btnClose"};
        bool clicked = ImGui::InvisibleButton(ids[index], ImVec2(buttonWidth, height));
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetItemRectMin();
        ImVec2 b = ImGui::GetItemRectMax();

        if (hovered) {
            ImU32 bg = (kind == WindowButton::Close) ? IM_COL32(200, 50, 50, 255)
                                                     : ui::col::kCardHover;
            dl->AddRectFilled(a, b, bg);
        }

        // Glyphs are centred on the button, not on a text baseline.
        ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
        const ImU32 fg = ui::col::kText;
        const float r  = 5.0f;

        switch (kind) {
            case WindowButton::Minimize:
                dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), fg, 1.3f);
                break;
            case WindowButton::Maximize:
                if (m_chrome.maximized()) {
                    // Two offset squares read as "restore".
                    dl->AddRect(ImVec2(c.x - r + 2, c.y - r), ImVec2(c.x + r + 2, c.y + r - 2),
                                fg, 0.0f, 0, 1.3f);
                    dl->AddRectFilled(ImVec2(c.x - r - 2, c.y - r + 2),
                                      ImVec2(c.x + r - 2, c.y + r), ui::col::kPanel);
                    dl->AddRect(ImVec2(c.x - r - 2, c.y - r + 2), ImVec2(c.x + r - 2, c.y + r),
                                fg, 0.0f, 0, 1.3f);
                } else {
                    dl->AddRect(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r),
                                fg, 0.0f, 0, 1.3f);
                }
                break;
            case WindowButton::Close:
                dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), fg, 1.4f);
                dl->AddLine(ImVec2(c.x + r, c.y - r), ImVec2(c.x - r, c.y + r), fg, 1.4f);
                break;
        }
        return clicked;
    };

    if (windowButton(WindowButton::Minimize, 0)) m_chrome.minimize();
    if (windowButton(WindowButton::Maximize, 1)) m_chrome.toggleMaximize();
    if (windowButton(WindowButton::Close,    2)) m_chrome.close();

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Any hovered widget must win over the drag strip, or buttons anywhere in
    // the caption row would be impossible to click.
    m_chrome.setBlockDrag(ImGui::IsAnyItemHovered() || ImGui::IsAnyItemActive());
}

void App::drawSidebar() {
    const float width = 216.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kPanel));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 18));
    ImGui::BeginChild("##sidebar", ImVec2(width, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

    // --- brand ---
    ImGui::PushFont(m_fonts.large);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kAccent));
    ImGui::TextUnformatted("DF Twitch");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::PushFont(m_fonts.small_);
    ui::hint("Stream " DFTS_VERSION);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 14));

    // One row: highlight, vector icon, label. Built by hand so the icon can be
    // drawn instead of typed as a character.
    auto navRow = [&](const char* key, ui::Icon icon, bool selected, float rowHeight) {
        ImGui::PushID(key);
        const bool clicked = ImGui::InvisibleButton("##row", ImVec2(-1, rowHeight));
        const bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();

        if (selected)      dl->AddRectFilled(a, b, ui::col::kAccentDim, 8.0f);
        else if (hovered)  dl->AddRectFilled(a, b, ui::col::kCardHover, 8.0f);

        const float iconBox = 18.0f;
        ui::drawIcon(dl, icon, ImVec2(a.x + 16.0f, (a.y + b.y) * 0.5f), iconBox,
                     selected ? IM_COL32_WHITE : ui::col::kText);

        const char*  text     = T(key);
        const ImVec2 textSize = ImGui::CalcTextSize(text);
        dl->AddText(ImVec2(a.x + 38.0f, (a.y + b.y - textSize.y) * 0.5f),
                    selected ? IM_COL32_WHITE : ui::col::kText, text);

        ImGui::PopID();
        return clicked;
    };

    // --- navigation ---
    for (const NavItem& item : kNavItems) {
        bool selected = (m_screen == item.screen);
        if (navRow(item.key, item.icon, selected, 40.0f)) setScreen(item.screen);

        // Live count badge next to "Following".
        if (item.screen == Screen::Following && !m_liveStreams.empty()) {
            ImGui::SameLine(width - 58);
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 32);
            ImGui::PushFont(m_fonts.small_);
            ui::badge(std::to_string(m_liveStreams.size()).c_str(), ui::col::kLive);
            ImGui::PopFont();
        }
        // Active download count next to "Downloads".
        if (item.screen == Screen::Downloads) {
            int active = 0;
            for (const auto& j : vod::DownloadManager::get().snapshot())
                if (!j.finished()) ++active;
            if (active > 0) {
                ImGui::SameLine(width - 58);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 32);
                ImGui::PushFont(m_fonts.small_);
                ui::badge(std::to_string(active).c_str(), ui::col::kAccent);
                ImGui::PopFont();
            }
        }
    }

    // --- bottom section ---
    float bottomHeight = 132.0f;
    ImGui::SetCursorPosY(ImGui::GetWindowHeight() - bottomHeight);

    if (navRow("nav.settings", ui::Icon::Settings, m_screen == Screen::Settings, 34.0f))
        setScreen(Screen::Settings);
    if (navRow("nav.logs", ui::Icon::Logs, m_screen == Screen::Logs, 34.0f))
        setScreen(Screen::Logs);
    if (navRow("nav.about", ui::Icon::About, m_screen == Screen::About, 34.0f))
        setScreen(Screen::About);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::drawTopBar() {
    const float height = 62.0f;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kBg));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 14));
    ImGui::BeginChild("##topbar", ImVec2(0, height),
                      ImGuiChildFlags_AlwaysUseWindowPadding, ImGuiWindowFlags_NoScrollbar);

    const char* title = T("nav.following");
    switch (m_screen) {
        case Screen::Following: title = T("following.title");  break;
        case Screen::Browse:    title = T("nav.browse");       break;
        case Screen::Vods:      title = T("vods.title");       break;
        case Screen::Recovery:  title = T("recovery.title");   break;
        case Screen::Downloads: title = T("downloads.title");  break;
        case Screen::Watch:     title = T("watch.title");      break;
        case Screen::Settings:  title = T("settings.title");   break;
        case Screen::Logs:      title = T("nav.logs");         break;
        case Screen::About:     title = T("nav.about");        break;
        default: break;
    }
    ImGui::PushFont(m_fonts.large);
    ImGui::TextUnformatted(title);
    ImGui::PopFont();

    // --- right-hand account block ---
    const Settings& s = Config::get().settings();
    std::string account = s.loggedIn() ? s.userDisplayName : T("login.start");
    float buttonWidth = std::max(140.0f, ImGui::CalcTextSize(account.c_str()).x + 40.0f);

    ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth - 22.0f);
    ImGui::SetCursorPosY(12.0f);
    if (s.loggedIn()) {
        if (ui::ghostButton((account + "##account").c_str(), buttonWidth, 34.0f))
            m_showLogoutConfirm = true;
    } else {
        if (ui::primaryButton((account + "##account").c_str(), buttonWidth, 34.0f))
            setScreen(Screen::Login);
    }

    // Busy indicator for in-flight network work.
    if (m_pendingJobs.load() > 0) {
        ImGui::SameLine(ImGui::GetWindowWidth() - buttonWidth - 60.0f);
        ImGui::SetCursorPosY(18.0f);
        ui::spinner(9.0f, 3.0f);
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void App::drawContent() {
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 8));
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kBg));
    ImGui::BeginChild("##content", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);

    switch (m_screen) {
        case Screen::Login:     drawLogin();     break;
        case Screen::Following: drawFollowing(); break;
        case Screen::Browse:    drawBrowse();    break;
        case Screen::Vods:      drawVods();      break;
        case Screen::Recovery:  drawRecovery();  break;
        case Screen::Downloads: drawDownloads(); break;
        case Screen::Watch:     drawWatch();     break;
        case Screen::Settings:  drawSettings();  break;
        case Screen::Logs:      drawLogs();      break;
        case Screen::About:     drawAbout();     break;
    }

    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
}

void App::drawToasts() {
    double now = ImGui::GetTime();
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                  [now](const Toast& t) { return t.expiresAt < now; }),
                   m_toasts.end());
    if (m_toasts.empty()) return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    float y = vp->WorkPos.y + vp->WorkSize.y - 24.0f;

    for (int i = (int)m_toasts.size() - 1; i >= 0; --i) {
        const Toast& t = m_toasts[i];
        ImVec2 textSize = ImGui::CalcTextSize(t.text.c_str());
        float width  = std::min(460.0f, textSize.x + 36.0f);
        float height = 44.0f;
        y -= height + 8.0f;

        ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + vp->WorkSize.x - width - 24.0f, y));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(ui::col::kCard));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));

        std::string id = "##toast" + std::to_string(i);
        ImGui::Begin(id.c_str(), nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
                     ImGuiWindowFlags_NoSavedSettings);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetWindowPos();
        dl->AddRectFilled(p, ImVec2(p.x + 4, p.y + height), t.color, 2.0f);

        ImGui::SetCursorPosX(14.0f);
        ui::textEllipsis(t.text.c_str(), width - 28.0f);
        ImGui::End();

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    }
}

void App::drawModals() {
    if (m_showLogoutConfirm) {
        ImGui::OpenPopup("##logout");
        m_showLogoutConfirm = false;
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    if (ImGui::BeginPopupModal("##logout", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar)) {
        // The dialog is centred on the window, which puts it right over the
        // player's native child window. Register it so the video is clipped
        // around it, otherwise the dialog is drawn behind the picture.
        const ImVec2 dialogPos  = ImGui::GetWindowPos();
        const ImVec2 dialogSize = ImGui::GetWindowSize();
        registerVideoOverlay(dialogPos.x, dialogPos.y, dialogSize.x, dialogSize.y);

        ImGui::PushFont(m_fonts.medium);
        ImGui::TextUnformatted(T("login.logoutConfirm"));
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 12));

        if (ui::dangerButton(T("login.logout"), 160, 36)) {
            logout();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if (ui::ghostButton(T("common.cancel"), 120, 36)) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Session
// ---------------------------------------------------------------------------
void App::restoreSession() {
    runAsync([this]() {
        Settings snapshot = Config::get().settings();
        std::string login, userId, error;
        int64_t expiresAt = 0;

        if (twitch::Auth::validate(snapshot.accessToken, login, userId, expiresAt, error)) {
            post([this, login, userId, expiresAt]() {
                Settings& s = Config::get().settings();
                s.userLogin   = login;
                s.userId      = userId;
                s.tokenExpiry = expiresAt;
                Config::get().save();
                refreshFollowing();
            });
            return;
        }

        // The stored token is stale; try the refresh token before giving up.
        if (!snapshot.refreshToken.empty()) {
            twitch::Auth::Session fresh;
            std::string refreshError;
            if (twitch::Auth::refresh(Config::get().effectiveClientId(),
                                      snapshot.refreshToken, fresh, refreshError)) {
                post([this, fresh]() {
                    Settings& s = Config::get().settings();
                    s.accessToken  = fresh.accessToken;
                    s.refreshToken = fresh.refreshToken;
                    s.tokenExpiry  = fresh.expiresAt;
                    Config::get().save();
                    LOG_I("session refreshed");
                    refreshFollowing();
                });
                return;
            }
        }

        post([this]() {
            Config::get().clearSession();
            setScreen(Screen::Login);
            toastError(T("error.tokenExpired"));
        });
    });
}

void App::startLogin() {
    if (m_loginInProgress) return;
    m_loginInProgress = true;
    m_loginError.clear();

    m_auth.begin(Config::get().effectiveClientId(),
                 [this](twitch::Auth::State state, const twitch::Auth::Session& session,
                        const std::string& error) {
        if (state != twitch::Auth::State::Success) {
            post([this, error]() {
                m_loginInProgress = false;
                m_loginError      = error;
                if (!error.empty()) toastError(I18n::get().fmt("login.failed", error));
            });
            return;
        }

        // Resolve who the token belongs to before showing the app.
        twitch::Api api(Config::get().effectiveClientId(), session.accessToken);
        auto user = api.getCurrentUser();

        post([this, session, user]() {
            Settings& s = Config::get().settings();
            s.accessToken  = session.accessToken;
            s.refreshToken = session.refreshToken;
            s.tokenExpiry  = session.expiresAt;
            if (user.ok) {
                s.userId          = user.value.id;
                s.userLogin       = user.value.login;
                s.userDisplayName = user.value.displayName;
            }
            Config::get().save();

            m_loginInProgress = false;
            toastOk(I18n::get().fmt("login.success",
                                    s.userDisplayName.empty() ? s.userLogin : s.userDisplayName));
            setScreen(Screen::Following);
            refreshFollowing();
        });
    });
}

void App::cancelLogin() {
    m_auth.cancel();
    m_loginInProgress = false;
}

void App::logout() {
    Settings& s = Config::get().settings();
    std::string token    = s.accessToken;
    std::string clientId = Config::get().effectiveClientId();

    runAsync([token, clientId]() { twitch::Auth::revoke(clientId, token); });

    m_chat.leave();
    m_player.stop();
    m_followed.clear();
    m_liveStreams.clear();
    m_vods.clear();
    Config::get().clearSession();
    setScreen(Screen::Login);
    LOG_I("signed out");
}

// ---------------------------------------------------------------------------
// Data loading
// ---------------------------------------------------------------------------
void App::refreshFollowing() {
    if (!Config::get().settings().loggedIn() || m_followingLoading) return;
    m_followingLoading  = true;
    m_lastFollowRefresh = ImGui::GetTime();

    runAsync([this]() {
        twitch::Api api = makeApi();
        std::string userId = Config::get().settings().userId;

        auto follows = api.getFollowedChannels(userId);
        auto streams = api.getFollowedStreams(userId);

        post([this, follows, streams]() {
            m_followingLoading = false;
            if (follows.ok) m_followed = follows.value;
            else if (!follows.error.empty())
                toastError(I18n::get().fmt("error.api", std::to_string(follows.status), follows.error));

            if (streams.ok) {
                m_liveStreams = streams.value;
                std::sort(m_liveStreams.begin(), m_liveStreams.end(),
                          [](const twitch::Stream& a, const twitch::Stream& b) {
                              return a.viewerCount > b.viewerCount;
                          });
            }
        });
    });
}

void App::loadVodsForChannel(const std::string& login) {
    std::string channel = util::lower(util::trim(login));
    if (channel.empty()) { toastError(T("error.noChannel")); return; }
    if (m_vodsLoading) return;

    m_vodsLoading = true;
    m_vods.clear();
    m_vodChannelLoaded = channel;

    static const char* kTypes[] = {"archive", "highlight", "upload", "all"};
    std::string type = kTypes[std::clamp(m_vodTypeIndex, 0, 3)];

    runAsync([this, channel, type]() {
        twitch::Api api = makeApi();
        auto users = api.getUsersByLogin({channel});
        if (!users.ok || users.value.empty()) {
            post([this, users]() {
                m_vodsLoading = false;
                toastError(users.error.empty() ? T("vods.empty") : users.error);
            });
            return;
        }

        auto videos = api.getVideosByUser(users.value[0].id, type, 100);
        post([this, videos]() {
            m_vodsLoading = false;
            if (videos.ok) m_vods = videos.value;
            else toastError(I18n::get().fmt("error.api", std::to_string(videos.status), videos.error));
        });
    });
}

// ---------------------------------------------------------------------------
// Playback watchdog
// ---------------------------------------------------------------------------
namespace {
// Twitch stitches ads into the HLS stream. When one finishes the broadcast is
// served under a fresh playlist, so the one we handed the player simply ends
// and playback stops with nothing to resume.
constexpr int    kMaxWatchRetries = 4;
constexpr double kStartGraceSecs  = 6.0;    // ignore state while opening
constexpr double kStallTimeout    = 25.0;   // opening/buffering for this long is dead

// A session that dies this quickly never reached live content: the pre-roll
// played and the stitched stream ended. Two of those in a row means retrying
// is just replaying adverts, so the watchdog stands down.
constexpr double kShortSessionSecs   = 45.0;
constexpr int    kShortSessionsToBail = 2;
} // namespace

void App::updatePlaybackWatchdog() {
    // Only live playback self-heals: a VOD that reaches its end is simply over.
    if (m_watchChannel.empty() || !m_watchIsLive) return;
    if (m_watchLoading || m_watchGaveUp || m_watchAdLoop) return;
    if (Config::get().settings().playerBackend != 0) return;  // state comes from libVLC
    if (m_playback.busy()) return;

    const double now = ImGui::GetTime();
    if (now - m_lastPlayStart < kStartGraceSecs) return;

    using State = player::VlcPlayer::State;
    const State state = m_vlc.state();

    const bool finished = (state == State::Ended || state == State::Error ||
                           state == State::Stopped || state == State::Idle);
    const bool stalled  = (state == State::Opening || state == State::Buffering) &&
                          (now - m_lastPlayStart > kStallTimeout);

    if (!finished && !stalled) {
        // Playing past the ad window means this really is the broadcast.
        if (state == State::Playing && now - m_lastPlayStart > kShortSessionSecs) {
            m_watchRetries      = 0;
            m_shortSessions     = 0;
            m_watchReconnecting = false;
        }
        return;
    }

    if (now < m_nextRetryAt) return;

    // How long did this attempt survive? Dying early is the advert signature.
    const double sessionLength = now - m_lastPlayStart;
    if (sessionLength < kShortSessionSecs) ++m_shortSessions;
    else                                   m_shortSessions = 0;

    if (m_shortSessions >= kShortSessionsToBail) {
        // Reconnecting again would only fetch another token, and every token
        // Twitch hands out starts with a fresh advert.
        m_watchAdLoop       = true;
        m_watchReconnecting = false;
        LOG_W("advert loop detected on " + m_watchChannel + "; stopping reconnects");
        return;
    }

    if (m_watchRetries >= kMaxWatchRetries) {
        m_watchGaveUp       = true;
        m_watchReconnecting = false;
        toastError(T("watch.reconnectFailed"));
        return;
    }

    ++m_watchRetries;
    // Back off gently so a genuinely offline channel is not hammered.
    m_nextRetryAt       = now + std::min(2.0 + m_watchRetries * 1.5, 10.0);
    m_watchReconnecting = true;

    LOG_I("playback ended unexpectedly; reconnecting to " + m_watchChannel +
          " (attempt " + std::to_string(m_watchRetries) + ")");
    reconnectLive();
}

void App::reconnectLive() {
    const std::string channel = m_watchChannel;
    if (channel.empty()) return;

    const uint64_t generation = ++m_watchGeneration;
    runAsync([this, channel, generation]() {
        twitch::PlaylistResult playlist = twitch::getLivePlaylist(channel);
        post([this, playlist, generation]() {
            if (generation != m_watchGeneration.load()) return;   // superseded

            if (!playlist.ok) {
                // Offline or still mid-ad; the watchdog will try again.
                m_playlist = {};
                return;
            }
            m_playlist = playlist;
            applyQuality(m_watchSubtitle.empty()
                             ? Config::get().settings().preferredQuality
                             : m_watchSubtitle);
        });
    });
}

// ---------------------------------------------------------------------------
// Channel extras: badges, room settings, metadata
// ---------------------------------------------------------------------------
std::string App::badgeUrl(const std::string& setId, const std::string& version) const {
    auto it = m_badgeUrls.find(setId + "/" + version);
    if (it != m_badgeUrls.end()) return it->second;
    // Some sets only publish version "1"; fall back so the badge still shows.
    it = m_badgeUrls.find(setId + "/1");
    return it == m_badgeUrls.end() ? std::string() : it->second;
}

std::string App::thirdPartyEmoteUrl(const std::string& word) const {
    if (!Config::get().settings().thirdPartyEmotes) return {};
    // Channel emotes shadow global ones, so they are checked first.
    if (auto it = m_roomEmotes.find(word); it != m_roomEmotes.end())   return it->second.url;
    if (auto it = m_globalEmotes.find(word); it != m_globalEmotes.end()) return it->second.url;
    return {};
}

void App::loadChannelExtras(const std::string& login) {
    if (login.empty()) return;

    m_chatSettingsValid = false;
    m_channelInfoValid  = false;

    const uint64_t generation = m_watchGeneration.load();
    runAsync([this, login, generation]() {
        twitch::Api api = makeApi();

        auto users = api.getUsersByLogin({login});
        if (!users.ok || users.value.empty()) return;
        const std::string broadcasterId = users.value[0].id;

        // Global badges cover most of what appears; channel badges add the
        // broadcaster's own subscriber and bit tiers on top.
        auto globalBadges  = api.getGlobalBadges();
        auto channelBadges = api.getChannelBadges(broadcasterId);
        auto settings      = api.getChatSettings(broadcasterId);
        auto info          = api.getChannelInfo(broadcasterId);

        // Community emote sets. The global ones only change between sessions,
        // so they are fetched once; the room's own set follows the channel.
        chat::EmoteTable globalEmotes;
        chat::EmoteTable roomEmotes;
        if (Config::get().settings().thirdPartyEmotes) {
            if (!m_globalEmotesLoaded) globalEmotes = chat::fetchGlobalEmotes();
            roomEmotes = chat::fetchChannelEmotes(broadcasterId);
        }

        std::unordered_map<std::string, std::string> urls;
        auto absorb = [&urls](const std::vector<twitch::BadgeSet>& sets) {
            for (const auto& set : sets)
                for (const auto& version : set.versions) {
                    std::string url = version.url2x.empty() ? version.url1x : version.url2x;
                    if (!url.empty()) urls[set.setId + "/" + version.id] = std::move(url);
                }
        };
        if (globalBadges.ok)  absorb(globalBadges.value);
        if (channelBadges.ok) absorb(channelBadges.value);   // channel wins on clash

        post([this, urls, settings, info, generation, globalEmotes, roomEmotes]() {
            if (generation != m_watchGeneration.load()) return;   // switched away
            m_badgeUrls = urls;
            if (settings.ok) { m_chatSettings = settings.value; m_chatSettingsValid = true; }
            if (info.ok)     { m_channelInfo  = info.value;     m_channelInfoValid  = true; }

            if (!globalEmotes.empty()) {
                m_globalEmotes       = globalEmotes;
                m_globalEmotesLoaded = true;
            }
            m_roomEmotes = roomEmotes;
        });
    });
}

// ---------------------------------------------------------------------------
// Browse
// ---------------------------------------------------------------------------
std::vector<twitch::Game> App::categoriesFromStreams(
        const std::vector<twitch::Stream>& streams) {
    // Group the live channels by category and rank by total viewers. Used when
    // Helix's own top-games list is unavailable, so the tab is never blank
    // while there are streams to show.
    std::vector<twitch::Game> out;
    std::unordered_map<std::string, size_t> indexByName;

    for (const auto& stream : streams) {
        if (stream.gameName.empty()) continue;

        auto it = indexByName.find(stream.gameName);
        if (it == indexByName.end()) {
            twitch::Game game;
            game.id          = stream.gameId;
            game.name        = stream.gameName;
            game.viewerCount = stream.viewerCount;
            indexByName[stream.gameName] = out.size();
            out.push_back(std::move(game));
        } else {
            out[it->second].viewerCount += stream.viewerCount;
            if (out[it->second].id.empty()) out[it->second].id = stream.gameId;
        }
    }

    std::sort(out.begin(), out.end(),
              [](const twitch::Game& a, const twitch::Game& b) {
                  return a.viewerCount > b.viewerCount;
              });
    return out;
}

void App::refreshBrowse() {
    if (m_browseLoading) return;
    m_browseLoading     = true;
    m_browseLoaded      = true;
    m_searchActive      = false;
    m_openCategory      = {};
    m_browseScrollReset = true;

    const std::string language = Config::get().settings().browseLanguage;
    runAsync([this, language]() {
        twitch::Api api = makeApi();
        auto games   = api.getTopGames(60);
        auto streams = api.getTopStreams(60, language);

        post([this, games, streams]() {
            m_browseLoading = false;
            if (streams.ok) m_topStreams = streams.value;

            if (games.ok && !games.value.empty()) {
                m_topGames = games.value;
            } else {
                // Do not fail quietly: a broken categories call used to leave
                // the tab silently empty while channels still filled in.
                if (!games.ok) {
                    LOG_W("games/top failed: " + games.error);
                    toastError(I18n::get().fmt("error.api", std::to_string(games.status),
                                               games.error));
                }
                // Fall back to the categories the live channels are streaming,
                // ranked by total viewers, so the tab still says something.
                m_topGames = categoriesFromStreams(m_topStreams);
            }

            if (!games.ok && !streams.ok) {
                m_browseLoaded = false;   // allow a retry on the next visit
                toastError(streams.error);
            }
        });
    });
}

void App::openCategory(const twitch::Game& game) {
    if (game.id.empty()) return;
    m_openCategory    = game;
    m_categoryStreams.clear();
    m_categoryLoading = true;
    // A different category is a different list; start it at the top.
    m_categoryScrollReset = true;

    std::string gameId = game.id;
    const std::string language = Config::get().settings().browseLanguage;
    runAsync([this, gameId, language]() {
        auto streams = makeApi().getStreamsByGameId(gameId, 80, language);
        post([this, streams]() {
            m_categoryLoading = false;
            if (streams.ok) m_categoryStreams = streams.value;
            else            toastError(streams.error);
        });
    });
}

void App::runSearch(const std::string& queryIn) {
    std::string query = util::trim(queryIn);
    if (query.empty()) return;
    if (!requireLogin()) return;

    m_browseLoading      = true;
    m_searchActive       = true;
    m_openCategory       = {};
    m_browseScrollReset  = true;   // new results, start at the top
    m_searchGames.clear();
    m_searchStreams.clear();

    const std::string language = Config::get().settings().browseLanguage;
    runAsync([this, query, language]() {
        twitch::Api api = makeApi();

        // A search term is usually a game name, so categories are resolved
        // first and their live streams are what the user actually wants.
        auto categories = api.searchCategories(query, 40);
        auto channels   = api.searchChannels(query, 40, /*liveOnly=*/true);

        std::vector<twitch::Stream> streams;
        if (categories.ok && !categories.value.empty()) {
            // Prefer an exact name match; otherwise take the top hit.
            const twitch::Game* best = &categories.value[0];
            for (const auto& g : categories.value)
                if (util::lower(g.name) == util::lower(query)) { best = &g; break; }

            auto inCategory = api.getStreamsByGameId(best->id, 60, language);
            if (inCategory.ok) streams = inCategory.value;
        }

        // Channels whose name matched are appended, skipping duplicates.
        if (channels.ok) {
            std::set<std::string> seen;
            for (const auto& s : streams) seen.insert(s.userId);
            for (const auto& s : channels.value)
                if (seen.insert(s.userId).second) streams.push_back(s);
        }

        post([this, categories, streams, query]() {
            m_browseLoading = false;
            if (categories.ok) m_searchGames = categories.value;
            m_searchStreams = streams;

            // Land on whichever tab actually has results.
            m_browseTab = m_searchGames.empty() ? 1 : 0;

            if (m_searchGames.empty() && m_searchStreams.empty())
                toast(I18n::get().fmt("browse.noResults", query), ui::col::kWarn);
        });
    });
}

void App::loadClipsForChannel(const std::string& login) {
    std::string channel = util::lower(util::trim(login));
    if (channel.empty()) { toastError(T("error.noChannel")); return; }
    if (m_clipsLoading) return;

    m_clipsLoading       = true;
    m_clips.clear();
    m_clipsChannelLoaded = channel;

    // Index -> how many days back to search; 0 means Twitch's all-time ranking.
    static const int kRanges[] = {1, 7, 30, 0};
    const int days = kRanges[std::clamp(m_clipRangeIndex, 0, 3)];

    runAsync([this, channel, days]() {
        twitch::Api api = makeApi();
        auto users = api.getUsersByLogin({channel});
        if (!users.ok || users.value.empty()) {
            post([this, users]() {
                m_clipsLoading = false;
                toastError(users.error.empty() ? T("clips.empty") : users.error);
            });
            return;
        }

        auto clips = api.getClips(users.value[0].id, 60, days);
        post([this, clips]() {
            m_clipsLoading = false;
            if (clips.ok) m_clips = clips.value;
            else toastError(I18n::get().fmt("error.api", std::to_string(clips.status),
                                            clips.error));
        });
    });
}

void App::downloadClip(const twitch::Clip& clip) {
    const std::string title = clip.broadcasterLogin + " - " +
                              util::formatLocalDateTime(clip.createdAtUnix) + " - " + clip.title;
    const std::string clipId   = clip.id;
    const std::string fallback = clip.guessMp4Url();

    toastOk(I18n::get().fmt("downloads.started", clip.title));

    runAsync([this, clipId, fallback, title]() {
        // Prefer the official download link; the derived URL is the backup for
        // when that endpoint is not available to this application.
        auto official = makeApi().getClipDownloadUrl(clipId);
        std::string url = official.ok ? official.value : std::string();
        if (url.empty()) url = fallback;

        post([this, url, title]() {
            if (url.empty()) { toastError(T("clips.noDownload")); return; }
            // A clip is a single MP4, so it bypasses the HLS segment path.
            vod::JobRequest req;
            req.playlistUrl  = url;
            req.title        = title;
            req.outputDir    = Config::get().effectiveDownloadDir();
            req.ffmpegPath   = Config::get().settings().ffmpegPath;
            req.threads      = 1;
            req.muxToMp4     = false;
            req.singleFile   = true;
            vod::DownloadManager::get().enqueue(req);
            setScreen(Screen::Downloads);
        });
    });
}

// ---------------------------------------------------------------------------
// Watching
// ---------------------------------------------------------------------------
void App::watchChannel(const std::string& login, const std::string& displayName) {
    std::string channel = util::lower(util::trim(login));
    if (channel.empty()) return;

    // Clicking the channel already on screen should not restart it.
    if (channel == m_watchChannel && m_watchIsLive && (playerPlaying() || m_watchLoading)) {
        setScreen(Screen::Watch);
        return;
    }

    const uint64_t generation = ++m_watchGeneration;

    // Fresh target: clear any reconnect history from the previous stream.
    m_watchRetries      = 0;
    m_shortSessions     = 0;
    m_watchGaveUp       = false;
    m_watchAdLoop       = false;
    m_watchReconnecting = false;
    m_nextRetryAt       = 0.0;

    setScreen(Screen::Watch);
    m_watchLoading  = true;
    m_watchIsLive   = true;
    m_watchTitle    = displayName.empty() ? channel : displayName;
    m_watchSubtitle.clear();
    m_playlist      = {};

    const Settings& s = Config::get().settings();
    // Reconnecting to the same chat room on every click just loses history.
    const bool sameRoom = (channel == m_watchChannel) && m_chat.connected();
    m_watchChannel = channel;
    if (s.chatEnabled && !sameRoom)
        m_chat.join(channel, s.userLogin, s.accessToken);
    if (!sameRoom) loadChannelExtras(channel);

    runAsync([this, channel, generation]() {
        twitch::PlaylistResult playlist = twitch::getLivePlaylist(channel);
        post([this, playlist, generation]() {
            // The user already moved to another channel; this result is stale.
            if (generation != m_watchGeneration.load()) return;

            m_watchLoading = false;
            m_playlist     = playlist;
            if (!playlist.ok) {
                toastError(playlist.error.empty() ? T("watch.streamOffline") : playlist.error);
                return;
            }
            applyQuality(Config::get().settings().preferredQuality);
        });
    });
}

void App::watchVod(const twitch::Vod& vod) {
    setScreen(Screen::Watch);
    m_watchLoading  = true;
    m_watchIsLive   = false;
    m_watchTitle    = vod.title.empty() ? ("VOD " + vod.id) : vod.title;
    m_watchSubtitle = vod.userName + "  -  " + util::formatLocalDateTime(vod.createdAtUnix);
    m_watchChannel.clear();   // VODs have no live chat to attach
    m_playlist      = {};
    m_chat.leave();

    std::string id = vod.id;
    const uint64_t generation = ++m_watchGeneration;
    runAsync([this, id, generation]() {
        twitch::PlaylistResult playlist = twitch::getVodPlaylist(id);
        post([this, playlist, generation]() {
            if (generation != m_watchGeneration.load()) return;   // superseded

            m_watchLoading = false;
            m_playlist     = playlist;
            if (!playlist.ok) {
                // Say what actually happened. Anonymous playback tokens cannot
                // open subscriber-only videos, and a deleted one is a job for
                // VOD Recovery rather than an error to stare at.
                if (playlist.status == 403)      toastError(T("watch.vodRestricted"));
                else if (playlist.status == 404) toastError(T("watch.vodGone"));
                else                             toastError(playlist.error);
                return;
            }
            applyQuality(Config::get().settings().preferredQuality);
        });
    });
}

void App::watchDirectUrl(const std::string& url, const std::string& title,
                         const std::string& chatChannel) {
    ++m_watchGeneration;   // nothing pending may override this choice
    setScreen(Screen::Watch);
    m_watchLoading  = false;
    m_watchIsLive   = false;
    m_watchTitle    = title;
    m_watchSubtitle = url;
    m_watchChannel  = chatChannel;
    m_playlist      = {};

    const Settings& s = Config::get().settings();
    if (!chatChannel.empty() && s.chatEnabled)
        m_chat.join(chatChannel, s.userLogin, s.accessToken);

    playerStart(url, title, /*live=*/false);
}

void App::applyQuality(const std::string& quality) {
    if (!m_playlist.ok) return;
    const twitch::Variant* variant = m_playlist.pick(quality);
    if (!variant) { toastError(T("watch.streamOffline")); return; }

    // Hand over the master playlist rather than the variant URL. A variant URL
    // is a short-lived, single-rendition playlist: when Twitch finishes an
    // advert it moves the broadcast to a different one and the old playlist
    // simply ends, which is why playback stopped and never resumed. The master
    // stays valid across that switch, so VLC's adaptive demuxer rides through
    // it. The chosen quality becomes a height ceiling instead.
    int  height    = variant->height;
    bool useMaster = !m_playlist.masterUrl.empty() && variant->name != "audio_only";

    if (useMaster && height <= 0) {
        // Some renditions omit RESOLUTION; recover the height from the name.
        height = std::atoi(variant->name.c_str());
        if (height <= 0) useMaster = false;   // cannot cap it, play it directly
    }

    const std::string url = useMaster ? m_playlist.masterUrl : variant->url;
    if (!playerStart(url, m_watchTitle, m_watchIsLive, useMaster ? height : 0)) return;

    m_watchSubtitle = variant->name;
}

void App::stopWatching() {
    // Invalidate anything still resolving so it cannot restart playback, and
    // stop the watchdog from treating a deliberate stop as a failure.
    ++m_watchGeneration;
    m_watchIsLive       = false;
    m_watchRetries      = 0;
    m_shortSessions     = 0;
    m_watchGaveUp       = false;
    m_watchAdLoop       = false;
    m_watchReconnecting = false;
    playerStop();
    m_chat.leave();
    m_watchTitle.clear();
    m_watchSubtitle.clear();
    m_watchChannel.clear();
    m_playlist = {};
}

// ---------------------------------------------------------------------------
// Downloads
// ---------------------------------------------------------------------------
void App::downloadVod(const twitch::Vod& vodItem) {
    std::string id    = vodItem.id;
    std::string title = vodItem.userLogin + " - " +
                        util::formatLocalDateTime(vodItem.createdAtUnix) + " - " + vodItem.title;

    toastOk(I18n::get().fmt("downloads.started", vodItem.title));

    runAsync([this, id, title]() {
        twitch::PlaylistResult playlist = twitch::getVodPlaylist(id);
        if (!playlist.ok) {
            post([this, playlist]() { toastError(playlist.error); });
            return;
        }
        const twitch::Variant* variant = playlist.pick(Config::get().settings().preferredQuality);
        if (!variant) {
            post([this]() { toastError(T("vods.empty")); });
            return;
        }
        std::string url = variant->url;
        post([this, url, title]() { downloadPlaylist(url, title); });
    });
}

void App::downloadPlaylist(const std::string& playlistUrl, const std::string& title) {
    const Settings& s = Config::get().settings();

    vod::JobRequest req;
    req.playlistUrl  = playlistUrl;
    req.title        = title;
    req.outputDir    = Config::get().effectiveDownloadDir();
    req.ffmpegPath   = s.ffmpegPath;
    req.threads      = s.downloadThreads;
    req.muxToMp4     = s.muxWithFfmpeg;
    req.keepSegments = s.keepSegments;

    vod::DownloadManager::get().enqueue(req);
    setScreen(Screen::Downloads);
}

// ---------------------------------------------------------------------------
// Recovery
// ---------------------------------------------------------------------------
void App::startRecoveryScan() {
    if (m_recScanning) return;

    vod::RecoveryQuery query;
    query.streamer = util::lower(util::trim(m_recStreamer));
    query.streamId = util::trim(m_recStreamId);

    std::string timeText = util::trim(m_recStartTime);
    if (!timeText.empty()) {
        std::string iso = util::replaceAll(timeText, " ", "T");
        if (iso.size() == 16) iso += ":00";          // accept "YYYY-MM-DD HH:MM"
        query.startTime = util::parseIso8601(iso);
    }

    if (!query.valid()) {
        m_recMessage = T("recovery.badInput");
        toastError(m_recMessage);
        return;
    }

    m_recScanning = true;
    m_recCancel   = false;
    m_recResults.clear();
    m_recSelectedUrl.clear();
    m_recMuted   = {};
    m_recMessage.clear();
    m_recProgress = {};

    // Remember the query so the user can re-run it later.
    std::string label = query.streamer + " / " + query.streamId + " / " + timeText;
    auto& recent = Config::get().settings().recentRecoveryUrls;
    if (std::find(recent.begin(), recent.end(), label) == recent.end()) {
        recent.insert(recent.begin(), label);
        if (recent.size() > 10) recent.resize(10);
        Config::get().save();
    }

    int window  = m_recWindow;
    int threads = std::max(8, Config::get().settings().downloadThreads);

    runAsync([this, query, window, threads]() {
        auto results = vod::scan(query, window, threads,
                                 [this](vod::ScanProgress p) {
                                     post([this, p]() { m_recProgress = p; });
                                 },
                                 &m_recCancel);

        if (results.empty()) {
            post([this]() {
                m_recScanning = false;
                m_recMessage  = T("recovery.notFound");
            });
            return;
        }

        // Widen the single hit into every rendition that exists alongside it.
        auto alternates = vod::findAlternateQualities(results.front(), &m_recCancel);
        if (alternates.empty()) alternates = results;

        // Count muted segments in the source rendition for the warning banner.
        vod::MutedInfo muted;
        http::Response body = http::get(alternates.front().url,
                                        {{"Referer", "https://www.twitch.tv/"}});
        if (body.ok()) muted = vod::countMutedSegments(body.body);

        post([this, alternates, muted]() {
            m_recScanning    = false;
            m_recResults     = alternates;
            m_recSelectedUrl = alternates.front().url;
            m_recMuted       = muted;
            m_recMessage     = T("recovery.found");
            toastOk(T("recovery.found"));
        });
    });
}

} // namespace dfts
