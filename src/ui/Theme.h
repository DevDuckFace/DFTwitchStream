#pragma once
#include <imgui.h>

namespace dfts::ui {

// Palette used by every screen. Kept in one place so the look stays coherent.
namespace col {
inline constexpr ImU32 kAccent      = IM_COL32(145,  71, 255, 255);  // Twitch purple
inline constexpr ImU32 kAccentHover = IM_COL32(164, 100, 255, 255);
inline constexpr ImU32 kAccentDim   = IM_COL32( 96,  46, 168, 255);
inline constexpr ImU32 kLive        = IM_COL32(233,  25,  22, 255);
inline constexpr ImU32 kOk          = IM_COL32( 60, 190, 120, 255);
inline constexpr ImU32 kWarn        = IM_COL32(230, 170,  60, 255);
inline constexpr ImU32 kError       = IM_COL32(230,  80,  80, 255);
inline constexpr ImU32 kBg          = IM_COL32( 14,  14,  18, 255);
inline constexpr ImU32 kPanel       = IM_COL32( 24,  24,  30, 255);
inline constexpr ImU32 kCard        = IM_COL32( 31,  31,  38, 255);
inline constexpr ImU32 kCardHover   = IM_COL32( 42,  42,  52, 255);
inline constexpr ImU32 kBorder      = IM_COL32( 52,  52,  64, 255);
inline constexpr ImU32 kTextDim     = IM_COL32(150, 150, 165, 255);
inline constexpr ImU32 kText        = IM_COL32(232, 232, 240, 255);
} // namespace col

void applyTheme();

// --- small reusable widgets -------------------------------------------------

// Filled accent button. `width` of 0 sizes to the label.
bool primaryButton(const char* label, float width = 0.0f, float height = 0.0f);
bool ghostButton(const char* label, float width = 0.0f, float height = 0.0f);
bool dangerButton(const char* label, float width = 0.0f, float height = 0.0f);

// High-visibility red button with white text outlined in black. Drawn by hand
// because ImGui has no text outline; used where an action must not be missed.
// Push a larger font before calling to scale the label.
bool alertButton(const char* label, float width, float height);

// Square button showing the standard four-corner fullscreen glyph: brackets
// opening inwards to enter, outwards to leave. Drawn rather than typed because
// the bundled fonts have no such symbol.
bool fullscreenButton(const char* id, float width, float height, bool exitIcon);

// --- navigation icons -------------------------------------------------------
// Drawn as vectors rather than glyphs: the bundled system fonts carry no
// symbol set, and ASCII stand-ins looked like punctuation in the sidebar.
enum class Icon {
    Following,   // heart
    Browse,      // compass
    Vods,        // film strip
    Recovery,    // clock with a restore arrow
    Downloads,   // arrow into a tray
    Watching,    // monitor with a play mark
    Settings,    // gear
    Logs,        // lines of text
    About,       // circled i
};

// `size` is the icon's full width/height; it is drawn centred on `center`.
void drawIcon(ImDrawList* dl, Icon icon, ImVec2 center, float size, ImU32 color);

// Rounded panel that spans the available width; call endCard() to finish.
void beginCard(const char* id, const ImVec2& size, bool* hovered = nullptr);
void endCard();

// Coloured pill, e.g. "LIVE" or a category name.
void badge(const char* text, ImU32 bg, ImU32 fg = IM_COL32_WHITE);

// Section heading with a subtle rule beneath it.
void sectionHeader(const char* text);

// Text truncated with an ellipsis to fit `maxWidth`.
void textEllipsis(const char* text, float maxWidth, ImU32 color = col::kText);

// Grey helper text.
void hint(const char* text);

// Indeterminate spinner drawn at the cursor.
void spinner(float radius = 8.0f, float thickness = 3.0f, ImU32 color = col::kAccent);

// Progress bar with a centred overlay label.
void progressBar(float fraction, const ImVec2& size, const char* overlay);

// Full-width single-line input with a placeholder.
bool inputText(const char* id, char* buf, size_t bufSize, const char* placeholder,
               float width = -1.0f, ImGuiInputTextFlags flags = 0);

// Centres the next item horizontally within the current window.
void centerNextItem(float itemWidth);

} // namespace dfts::ui
