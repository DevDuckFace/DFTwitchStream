#pragma once

namespace dfts::player {

// A borderless child window layered over the application's OpenGL surface.
// Video backends render into it, so the picture appears inside the UI instead
// of in a separate top-level window.
class VideoSurface {
public:
    VideoSurface() = default;
    ~VideoSurface();

    VideoSurface(const VideoSurface&)            = delete;
    VideoSurface& operator=(const VideoSurface&) = delete;

    // `parentHwnd` is the main window handle.
    bool create(void* parentHwnd);
    void destroy();

    void* handle() const { return m_hwnd; }
    bool  valid()  const { return m_hwnd != nullptr; }

    // Position in pixels, relative to the parent's client area.
    void setBounds(int x, int y, int width, int height);
    void setVisible(bool visible);
    bool visible() const { return m_visible; }

    // Repaints the black backdrop, used when playback stops.
    void clear();

    // Punches a hole in the video window, in coordinates local to it.
    //
    // The surface is a native child window sitting on top of the OpenGL
    // canvas, so anything the UI draws over it is hidden. Excluding a
    // rectangle lets a menu or overlay show through without resizing the
    // video. Pass a zero-sized rectangle to restore the full surface.
    void setClipExclusion(int x, int y, int width, int height);

private:
    void* m_parent  = nullptr;
    void* m_hwnd    = nullptr;
    bool  m_visible = false;

    // Last applied exclusion, so the region is only rebuilt when it changes.
    int   m_clipX = 0, m_clipY = 0, m_clipW = 0, m_clipH = 0;
    int   m_lastWidth = 0, m_lastHeight = 0;
};

} // namespace dfts::player
