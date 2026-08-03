#include "player/VideoSurface.h"
#include "core/Log.h"

#include <windows.h>
#include <algorithm>

namespace dfts::player {

namespace {

constexpr const wchar_t* kClassName = L"DFTS_VideoSurface";

LRESULT CALLBACK surfaceProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_ERASEBKGND: {
            HDC dc = (HDC)wp;
            RECT rc{};
            GetClientRect(hwnd, &rc);
            HBRUSH brush = CreateSolidBrush(RGB(10, 10, 13));
            FillRect(dc, &rc, brush);
            DeleteObject(brush);
            return 1;
        }
        // Keyboard belongs to the ImGui window, not to the video child.
        case WM_KEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_SYSKEYUP: {
            if (HWND parent = GetParent(hwnd)) return SendMessageW(parent, msg, wp, lp);
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
    wc.lpfnWndProc   = surfaceProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = kClassName;
    RegisterClassExW(&wc);
    registered = true;
}

} // namespace

VideoSurface::~VideoSurface() { destroy(); }

bool VideoSurface::create(void* parentHwnd) {
    if (m_hwnd) return true;
    if (!parentHwnd) return false;

    registerClassOnce();
    m_parent = parentHwnd;
    m_hwnd = CreateWindowExW(0, kClassName, L"",
                             WS_CHILD | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                             0, 0, 16, 16, (HWND)parentHwnd, nullptr,
                             GetModuleHandleW(nullptr), nullptr);
    if (!m_hwnd) {
        LOG_E("cannot create the video surface window");
        return false;
    }
    return true;
}

void VideoSurface::destroy() {
    if (m_hwnd) {
        DestroyWindow((HWND)m_hwnd);
        m_hwnd = nullptr;
    }
    m_parent  = nullptr;
    m_visible = false;
}

void VideoSurface::setBounds(int x, int y, int width, int height) {
    if (!m_hwnd) return;
    width  = std::max(1, width);
    height = std::max(1, height);
    SetWindowPos((HWND)m_hwnd, HWND_TOP, x, y, width, height,
                 SWP_NOACTIVATE | (m_visible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));

    // The clip region is expressed in window coordinates, so a resize has to
    // rebuild it or the hole would keep the old geometry.
    if (width != m_lastWidth || height != m_lastHeight) {
        m_lastWidth  = width;
        m_lastHeight = height;
        if (m_clipW > 0 && m_clipH > 0) {
            int x0 = m_clipX, y0 = m_clipY, w0 = m_clipW, h0 = m_clipH;
            m_clipW = 0;                       // force the rebuild below
            setClipExclusion(x0, y0, w0, h0);
        }
    }
}

void VideoSurface::setClipExclusion(int x, int y, int width, int height) {
    if (!m_hwnd) return;

    // Rebuilding a GDI region every frame is wasteful; only react to changes.
    if (x == m_clipX && y == m_clipY && width == m_clipW && height == m_clipH) return;
    m_clipX = x; m_clipY = y; m_clipW = width; m_clipH = height;

    if (width <= 0 || height <= 0) {
        SetWindowRgn((HWND)m_hwnd, nullptr, TRUE);   // full surface again
        return;
    }

    RECT client{};
    GetClientRect((HWND)m_hwnd, &client);
    const int clientW = client.right - client.left;
    const int clientH = client.bottom - client.top;
    if (clientW <= 0 || clientH <= 0) return;

    // Never let a bad rectangle blank the picture: clamp the hole to the
    // surface and ignore it if it would swallow most of the video.
    int x0 = std::max(x, (int)client.left);
    int y0 = std::max(y, (int)client.top);
    int x1 = std::min(x + width,  (int)client.right);
    int y1 = std::min(y + height, (int)client.bottom);
    if (x1 <= x0 || y1 <= y0) {
        SetWindowRgn((HWND)m_hwnd, nullptr, TRUE);
        return;
    }
    const double covered = (double)(x1 - x0) * (y1 - y0) / ((double)clientW * clientH);
    if (covered > 0.6) {
        SetWindowRgn((HWND)m_hwnd, nullptr, TRUE);
        return;
    }

    HRGN full = CreateRectRgn(client.left, client.top, client.right, client.bottom);
    HRGN hole = CreateRectRgn(x0, y0, x1, y1);
    CombineRgn(full, full, hole, RGN_DIFF);
    DeleteObject(hole);

    // SetWindowRgn takes ownership of the region on success.
    SetWindowRgn((HWND)m_hwnd, full, TRUE);
}

void VideoSurface::setVisible(bool visible) {
    if (m_visible == visible) return;
    m_visible = visible;
    if (m_hwnd) ShowWindow((HWND)m_hwnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);
}

void VideoSurface::clear() {
    if (m_hwnd) InvalidateRect((HWND)m_hwnd, nullptr, TRUE);
}

} // namespace dfts::player
