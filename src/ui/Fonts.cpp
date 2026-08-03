#include "ui/Fonts.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>
#include <string>
#include <vector>

namespace dfts::ui {

namespace {

FontSet g_fonts;

std::string systemFontPath(const char* fileName) {
    wchar_t dir[MAX_PATH]{};
    GetWindowsDirectoryW(dir, MAX_PATH);
    return util::narrow(dir) + "\\Fonts\\" + fileName;
}

// Latin + Latin-1 + Latin Extended-A covers English and Portuguese accents,
// and the punctuation range keeps chat symbols from rendering as boxes.
const ImWchar* glyphRanges() {
    static const ImWchar ranges[] = {
        0x0020, 0x00FF,   // Basic Latin + Latin-1 Supplement
        0x0100, 0x017F,   // Latin Extended-A
        0x2000, 0x206F,   // General punctuation
        0x20A0, 0x20BF,   // Currency symbols
        0x2190, 0x21FF,   // Arrows
        0x2600, 0x26FF,   // Misc symbols
        0,
    };
    return ranges;
}

ImFont* addFont(ImGuiIO& io, const std::string& path, float sizePx) {
    if (!util::fileExists(path)) return nullptr;
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    return io.Fonts->AddFontFromFileTTF(path.c_str(), sizePx, &cfg, glyphRanges());
}

} // namespace

FontSet loadFonts(float scale) {
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    const std::string regularPath = systemFontPath("segoeui.ttf");
    const std::string mediumPath  = util::fileExists(systemFontPath("seguisb.ttf"))
                                  ? systemFontPath("seguisb.ttf")   // Segoe UI Semibold
                                  : systemFontPath("segoeuib.ttf"); // fall back to Bold
    const std::string boldPath    = systemFontPath("segoeuib.ttf");

    if (scale < 0.6f) scale = 0.6f;
    if (scale > 2.5f) scale = 2.5f;

    FontSet set;
    set.regular = addFont(io, regularPath, 16.0f * scale);
    set.small_  = addFont(io, regularPath, 13.0f * scale);
    set.medium  = addFont(io, mediumPath,  16.0f * scale);
    set.large   = addFont(io, boldPath,    22.0f * scale);
    set.huge    = addFont(io, boldPath,    44.0f * scale);

    if (!set.regular) {
        LOG_W("Segoe UI not found; falling back to the built-in font");
        ImFont* fallback = io.Fonts->AddFontDefault();
        set = {fallback, fallback, fallback, fallback, fallback};
    }
    // Any slot that failed to load falls back to the body font.
    if (!set.small_) set.small_ = set.regular;
    if (!set.medium) set.medium = set.regular;
    if (!set.large)  set.large  = set.medium;
    if (!set.huge)   set.huge   = set.large;

    io.Fonts->Build();
    io.FontDefault = set.regular;

    g_fonts = set;
    return set;
}

const FontSet& fonts() { return g_fonts; }

} // namespace dfts::ui
