#pragma once

namespace dfts {

// Removes the Windows caption bar while keeping everything the native frame
// gives us for free: resize borders, Aero Snap, drop shadow and the window
// animations. The application draws its own title bar into the reclaimed space.
//
// This is done by intercepting WM_NCCALCSIZE (so the client area covers the
// caption) and WM_NCHITTEST (so a strip at the top still behaves like a title
// bar for dragging and double-click maximise).
class WindowChrome {
public:
    bool install(void* hwnd);
    void uninstall();

    // Height of the custom title bar, in pixels.
    void setCaptionHeight(int px) { m_captionHeight = px; }
    int  captionHeight() const    { return m_captionHeight; }

    // Called once per frame: when the UI has an interactive widget under the
    // cursor, the caption strip must not swallow the click.
    void setBlockDrag(bool blocked) { m_blockDrag = blocked; }

    bool maximized() const;
    void minimize();
    void toggleMaximize();
    void close();

    // Borderless fullscreen on the monitor the window currently sits on --
    // never assumes the primary display. Restores the exact previous
    // placement, including a maximised state, when switched off.
    bool fullscreen() const { return m_fullscreen; }
    void setFullscreen(bool enabled);
    void toggleFullscreen() { setFullscreen(!m_fullscreen); }

private:
    static long long __stdcall wndProc(void* hwnd, unsigned msg,
                                       unsigned long long wp, long long lp);

    void*     m_hwnd          = nullptr;
    void*     m_originalProc  = nullptr;
    int       m_captionHeight = 38;
    bool      m_blockDrag     = false;

    // Window state saved on the way into fullscreen.
    bool          m_fullscreen   = false;
    long long     m_savedStyle   = 0;
    long long     m_savedExStyle = 0;
    unsigned char m_savedPlacement[64]{};   // holds a WINDOWPLACEMENT
};

} // namespace dfts
