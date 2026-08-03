#pragma once
#include "twitch/TwitchApi.h"
#include "ui/Fonts.h"

#include <imgui.h>
#include <string>
#include <vector>

namespace dfts::ui {

// Draws a remote image into `size`, cropped to fill and rounded. Falls back to
// a placeholder block while the download is still in flight.
// `hovered` slightly brightens the image so cards react to the pointer.
void thumbnail(const std::string& url, const ImVec2& size, bool hovered = false);

// Which button of which card the user pressed this frame (-1 = none).
struct StreamGridAction {
    int watchIndex = -1;
    int vodsIndex  = -1;
};

// Responsive grid of live-stream cards with thumbnail, title, category,
// viewers and uptime. Shared by the Following and Browse screens so both look
// identical.
StreamGridAction drawStreamGrid(const char* idPrefix,
                                const std::vector<twitch::Stream>& streams,
                                const FontSet& fonts, int columns);

} // namespace dfts::ui
