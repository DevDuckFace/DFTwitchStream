#pragma once
#include <imgui.h>

namespace dfts::ui {

// Loads UI fonts from the system font directory. Segoe UI is present on every
// supported Windows version; if loading fails the ImGui default font is kept.
struct FontSet {
    ImFont* regular = nullptr;   // body text
    ImFont* medium  = nullptr;   // buttons, labels
    ImFont* large   = nullptr;   // page headings
    ImFont* huge    = nullptr;   // login code, hero numbers
    ImFont* small_  = nullptr;   // timestamps, metadata
};

// `scale` multiplies every size (the UI scale setting).
FontSet loadFonts(float scale);

const FontSet& fonts();

} // namespace dfts::ui
