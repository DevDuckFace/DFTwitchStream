#include "app/WindowChrome.h"
#include "core/Log.h"

#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>   // SHAppBarMessage, for auto-hide task bar detection

namespace dfts {

namespace {

// Only one main window exists, so a single instance pointer is enough to route
// the subclassed messages back to the owning object.
WindowChrome* g_instance = nullptr;

// Width of the invisible grab area along each edge.
constexpr int kResizeBorder = 8;

int frameBorderThickness(HWND hwnd) {
    UINT dpi = GetDpiForWindow(hwnd);
    if (dpi == 0) dpi = 96;
    return GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi) +
           GetSystemMetricsForDpi(SM_CYSIZEFRAME, dpi);
}

} // namespace

bool WindowChrome::install(void* hwnd) {
    if (!hwnd) return false;
    m_hwnd     = hwnd;
    g_instance = this;

    m_originalProc = (void*)SetWindowLongPtrW((HWND)hwnd, GWLP_WNDPROC,
                                              (LONG_PTR)&WindowChrome::wndProc);
    if (!m_originalProc) {
        LOG_E("cannot subclass the window for custom chrome");
        g_instance = nullptr;
        return false;
    }

    // Forces the frame to be recalculated so WM_NCCALCSIZE runs immediately.
    SetWindowPos((HWND)hwnd, nullptr, 0, 0, 0, 0,
                 SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    return true;
}

void WindowChrome::uninstall() {
    if (m_hwnd && m_originalProc)
        SetWindowLongPtrW((HWND)m_hwnd, GWLP_WNDPROC, (LONG_PTR)m_originalProc);
    m_hwnd         = nullptr;
    m_originalProc = nullptr;
    g_instance     = nullptr;
}

bool WindowChrome::maximized() const {
    if (!m_hwnd) return false;
    WINDOWPLACEMENT wp{sizeof(wp)};
    GetWindowPlacement((HWND)m_hwnd, &wp);
    return wp.showCmd == SW_SHOWMAXIMIZED;
}

void WindowChrome::minimize() {
    if (m_hwnd) ShowWindow((HWND)m_hwnd, SW_MINIMIZE);
}

void WindowChrome::toggleMaximize() {
    if (m_hwnd) ShowWindow((HWND)m_hwnd, maximized() ? SW_RESTORE : SW_MAXIMIZE);
}

void WindowChrome::close() {
    if (m_hwnd) PostMessageW((HWND)m_hwnd, WM_CLOSE, 0, 0);
}

void WindowChrome::setFullscreen(bool enabled) {
    if (!m_hwnd || enabled == m_fullscreen) return;
    HWND hwnd = (HWND)m_hwnd;

    static_assert(sizeof(WINDOWPLACEMENT) <= sizeof(m_savedPlacement),
                  "saved placement buffer is too small");
    auto* placement = reinterpret_cast<WINDOWPLACEMENT*>(m_savedPlacement);

    if (enabled) {
        placement->length = sizeof(WINDOWPLACEMENT);
        GetWindowPlacement(hwnd, placement);
        m_savedStyle   = GetWindowLongPtrW(hwnd, GWL_STYLE);
        m_savedExStyle = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);

        // MONITOR_DEFAULTTONEAREST resolves the display the window is actually
        // on, so a second screen stays fullscreen on that screen.
        MONITORINFO info{sizeof(info)};
        if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
            LOG_W("cannot resolve the monitor for fullscreen");
            return;
        }

        // Set the flag *before* touching the frame: SWP_FRAMECHANGED sends
        // WM_NCCALCSIZE synchronously, and the window is still flagged as
        // maximised at that point. With the flag still false the handler took
        // the maximised path and shrank the client area to the work area, so
        // the client no longer matched the window and the video landed outside
        // it -- a grey screen with the controls still on top.
        m_fullscreen = true;

        // WS_MAXIMIZE has to go as well: left set, the window stays flagged as
        // zoomed while being a popup, which confuses later frame calculations.
        SetWindowLongPtrW(hwnd, GWL_STYLE,
                          (m_savedStyle & ~(LONG_PTR)(WS_OVERLAPPEDWINDOW | WS_MAXIMIZE)) |
                              WS_POPUP);

        // rcMonitor, not rcWork: fullscreen covers the task bar too.
        //
        // The extra pixel of height is deliberate. A borderless window whose
        // bounds match a monitor exactly is eligible for direct presentation
        // (independent flip), and in that mode the compositor stops drawing
        // our child windows over the swapchain -- the video kept rendering
        // (screen captures still showed it) but the screen only showed the
        // OpenGL surface. Being one pixel off keeps the window composited.
        SetWindowPos(hwnd, HWND_TOP,
                     info.rcMonitor.left, info.rcMonitor.top,
                     info.rcMonitor.right  - info.rcMonitor.left,
                     info.rcMonitor.bottom - info.rcMonitor.top + 1,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        LOG_I("entered fullscreen on the current monitor");
    } else {
        m_fullscreen = false;
        SetWindowLongPtrW(hwnd, GWL_STYLE,   m_savedStyle);
        SetWindowLongPtrW(hwnd, GWL_EXSTYLE, m_savedExStyle);
        SetWindowPlacement(hwnd, placement);
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        LOG_I("left fullscreen");
    }
}

long long __stdcall WindowChrome::wndProc(void* hwndRaw, unsigned msg,
                                          unsigned long long wp, long long lp) {
    HWND hwnd = (HWND)hwndRaw;
    WindowChrome* self = g_instance;
    if (!self || !self->m_originalProc)
        return DefWindowProcW(hwnd, msg, (WPARAM)wp, (LPARAM)lp);

    auto callOriginal = [&]() {
        return (long long)CallWindowProcW((WNDPROC)self->m_originalProc, hwnd, msg,
                                          (WPARAM)wp, (LPARAM)lp);
    };

    switch (msg) {
        // Report the whole window as client area, which hides the caption but
        // leaves the resize frame intact.
        case WM_NCCALCSIZE: {
            if (wp == FALSE) break;

            auto* params = (NCCALCSIZE_PARAMS*)lp;
            RECT& rect = params->rgrc[0];

            // The default handler is deliberately NOT called: it would inset
            // the rectangle by the frame, and insetting again below left a
            // strip of bare frame visible along the window edges.
            // Fullscreen and restored windows both want the client area to be
            // the whole window; only a maximised one needs the inset below.
            if (self->m_fullscreen || !IsZoomed(hwnd)) return 0;

            // Maximised, Windows grows the window past the monitor by the frame
            // thickness. Pull it back in, then clamp to the work area so the
            // task bar is neither covered nor overlapped.
            const int border = frameBorderThickness(hwnd);
            rect.left   += border;
            rect.top    += border;
            rect.right  -= border;
            rect.bottom -= border;

            MONITORINFO info{sizeof(info)};
            if (GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &info)) {
                rect.left   = (rect.left   > info.rcWork.left)   ? rect.left   : info.rcWork.left;
                rect.top    = (rect.top    > info.rcWork.top)    ? rect.top    : info.rcWork.top;
                rect.right  = (rect.right  < info.rcWork.right)  ? rect.right  : info.rcWork.right;
                rect.bottom = (rect.bottom < info.rcWork.bottom) ? rect.bottom : info.rcWork.bottom;

                // An auto-hiding task bar needs one pixel left uncovered or it
                // stops popping up.
                APPBARDATA abd{sizeof(abd)};
                if (SHAppBarMessage(ABM_GETSTATE, &abd) & ABS_AUTOHIDE) rect.bottom -= 1;
            }
            return 0;
        }

        // Map the top strip to the caption so dragging and double-click work,
        // and keep the outer edges as resize handles.
        case WM_NCHITTEST: {
            // Fullscreen has no frame to resize and no caption to drag.
            if (self->m_fullscreen) return HTCLIENT;

            POINT cursor{GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
            RECT window{};
            GetWindowRect(hwnd, &window);

            const bool maximised = IsZoomed(hwnd);
            if (!maximised) {
                const bool left   = cursor.x < window.left   + kResizeBorder;
                const bool right  = cursor.x >= window.right - kResizeBorder;
                const bool top    = cursor.y < window.top    + kResizeBorder;
                const bool bottom = cursor.y >= window.bottom - kResizeBorder;

                if (top    && left)  return HTTOPLEFT;
                if (top    && right) return HTTOPRIGHT;
                if (bottom && left)  return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left)   return HTLEFT;
                if (right)  return HTRIGHT;
                if (top)    return HTTOP;
                if (bottom) return HTBOTTOM;
            }

            // Anything the UI claims (buttons, sliders, text fields) stays a
            // normal client click; the rest of the strip drags the window.
            if (!self->m_blockDrag &&
                cursor.y < window.top + self->m_captionHeight + (maximised ? kResizeBorder : 0))
                return HTCAPTION;

            return HTCLIENT;
        }

        // WM_GETMINMAXINFO is intentionally left to GLFW: clamping the
        // maximised size here as well as in WM_NCCALCSIZE double-corrected the
        // geometry, and GLFW's own handler ran afterwards and undid it anyway.

        default: break;
    }

    return callOriginal();
}

} // namespace dfts
