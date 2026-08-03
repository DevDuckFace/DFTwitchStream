#include "ui/Theme.h"

#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace dfts::ui {

namespace {

ImVec4 toVec4(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }

bool coloredButton(const char* label, ImU32 base, ImU32 hovered, ImU32 active,
                   float width, float height) {
    ImGui::PushStyleColor(ImGuiCol_Button,        toVec4(base));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, toVec4(hovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  toVec4(active));
    bool clicked = ImGui::Button(label, ImVec2(width, height));
    ImGui::PopStyleColor(3);
    return clicked;
}

} // namespace

void applyTheme() {
    ImGuiStyle& s = ImGui::GetStyle();

    s.WindowRounding    = 10.0f;
    s.ChildRounding     = 10.0f;
    s.FrameRounding     = 8.0f;
    s.PopupRounding     = 10.0f;
    s.ScrollbarRounding = 10.0f;
    s.GrabRounding      = 8.0f;
    s.TabRounding       = 8.0f;

    s.WindowBorderSize  = 0.0f;
    s.ChildBorderSize   = 1.0f;
    s.FrameBorderSize   = 0.0f;
    s.PopupBorderSize   = 1.0f;

    s.WindowPadding     = ImVec2(16, 16);
    s.FramePadding      = ImVec2(12,  8);
    s.CellPadding       = ImVec2(10,  6);
    s.ItemSpacing       = ImVec2(10, 10);
    s.ItemInnerSpacing  = ImVec2(8,   6);
    s.ScrollbarSize     = 12.0f;
    s.GrabMinSize       = 12.0f;

    s.WindowTitleAlign  = ImVec2(0.0f, 0.5f);
    s.ButtonTextAlign   = ImVec2(0.5f, 0.5f);
    s.SelectableTextAlign = ImVec2(0.0f, 0.5f);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text]                  = toVec4(col::kText);
    c[ImGuiCol_TextDisabled]          = toVec4(col::kTextDim);
    c[ImGuiCol_WindowBg]              = toVec4(col::kBg);
    c[ImGuiCol_ChildBg]               = toVec4(col::kPanel);
    c[ImGuiCol_PopupBg]               = toVec4(col::kPanel);
    c[ImGuiCol_Border]                = toVec4(col::kBorder);
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_FrameBg]               = toVec4(col::kCard);
    c[ImGuiCol_FrameBgHovered]        = toVec4(col::kCardHover);
    c[ImGuiCol_FrameBgActive]         = toVec4(col::kCardHover);
    c[ImGuiCol_TitleBg]               = toVec4(col::kPanel);
    c[ImGuiCol_TitleBgActive]         = toVec4(col::kPanel);
    c[ImGuiCol_TitleBgCollapsed]      = toVec4(col::kPanel);
    c[ImGuiCol_MenuBarBg]             = toVec4(col::kPanel);
    c[ImGuiCol_ScrollbarBg]           = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ScrollbarGrab]         = toVec4(col::kBorder);
    c[ImGuiCol_ScrollbarGrabHovered]  = toVec4(col::kAccentDim);
    c[ImGuiCol_ScrollbarGrabActive]   = toVec4(col::kAccent);
    c[ImGuiCol_CheckMark]             = toVec4(col::kAccent);
    c[ImGuiCol_SliderGrab]            = toVec4(col::kAccent);
    c[ImGuiCol_SliderGrabActive]      = toVec4(col::kAccentHover);
    c[ImGuiCol_Button]                = toVec4(col::kCard);
    c[ImGuiCol_ButtonHovered]         = toVec4(col::kCardHover);
    c[ImGuiCol_ButtonActive]          = toVec4(col::kAccentDim);
    c[ImGuiCol_Header]                = toVec4(col::kCard);
    c[ImGuiCol_HeaderHovered]         = toVec4(col::kCardHover);
    c[ImGuiCol_HeaderActive]          = toVec4(col::kAccentDim);
    c[ImGuiCol_Separator]             = toVec4(col::kBorder);
    c[ImGuiCol_SeparatorHovered]      = toVec4(col::kAccentDim);
    c[ImGuiCol_SeparatorActive]       = toVec4(col::kAccent);
    c[ImGuiCol_ResizeGrip]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_ResizeGripHovered]     = toVec4(col::kAccentDim);
    c[ImGuiCol_ResizeGripActive]      = toVec4(col::kAccent);
    c[ImGuiCol_Tab]                   = toVec4(col::kPanel);
    c[ImGuiCol_TabHovered]            = toVec4(col::kCardHover);
    c[ImGuiCol_TabSelected]           = toVec4(col::kCard);
    c[ImGuiCol_TabDimmed]             = toVec4(col::kPanel);
    c[ImGuiCol_TabDimmedSelected]     = toVec4(col::kCard);
    c[ImGuiCol_PlotHistogram]         = toVec4(col::kAccent);
    c[ImGuiCol_PlotHistogramHovered]  = toVec4(col::kAccentHover);
    c[ImGuiCol_TableHeaderBg]         = toVec4(col::kPanel);
    c[ImGuiCol_TableBorderStrong]     = toVec4(col::kBorder);
    c[ImGuiCol_TableBorderLight]      = toVec4(col::kBorder);
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.02f);
    c[ImGuiCol_TextSelectedBg]        = toVec4(col::kAccentDim);
    c[ImGuiCol_DragDropTarget]        = toVec4(col::kAccent);
    c[ImGuiCol_NavCursor]             = toVec4(col::kAccent);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0, 0, 0, 0.65f);
}

bool primaryButton(const char* label, float width, float height) {
    return coloredButton(label, col::kAccent, col::kAccentHover, col::kAccentDim, width, height);
}

bool ghostButton(const char* label, float width, float height) {
    return coloredButton(label, col::kCard, col::kCardHover, col::kAccentDim, width, height);
}

bool dangerButton(const char* label, float width, float height) {
    return coloredButton(label, IM_COL32(120, 40, 40, 255), IM_COL32(160, 55, 55, 255),
                         IM_COL32(190, 65, 65, 255), width, height);
}

bool alertButton(const char* label, float width, float height) {
    ImGui::PushID(label);

    // Anything after "##" is an id, not something to render.
    const char* displayEnd = label;
    while (*displayEnd && !(displayEnd[0] == '#' && displayEnd[1] == '#')) ++displayEnd;

    const ImVec2 size(width, height);
    const bool clicked = ImGui::InvisibleButton("##hit", size);
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();

    ImU32 fill = held    ? IM_COL32(150, 20, 20, 255)
               : hovered ? IM_COL32(232, 70, 70, 255)
                         : IM_COL32(200, 38, 38, 255);
    dl->AddRectFilled(a, b, fill, 8.0f);
    dl->AddRect(a, b, IM_COL32(0, 0, 0, 200), 8.0f, 0, 1.5f);

    const ImVec2 textSize = ImGui::CalcTextSize(label, displayEnd);
    const ImVec2 pos((a.x + b.x - textSize.x) * 0.5f, (a.y + b.y - textSize.y) * 0.5f);

    // Eight offset copies form the outline, then the white glyphs on top.
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            if (dx == 0 && dy == 0) continue;
            dl->AddText(ImVec2(pos.x + dx, pos.y + dy), IM_COL32(0, 0, 0, 255),
                        label, displayEnd);
        }
    }
    dl->AddText(pos, IM_COL32_WHITE, label, displayEnd);

    ImGui::PopID();
    return clicked;
}

void drawIcon(ImDrawList* dl, Icon icon, ImVec2 c, float size, ImU32 color) {
    const float r  = size * 0.5f;      // half extent
    const float th = std::max(1.4f, size * 0.09f);

    switch (icon) {
        case Icon::Following: {
            // Heart: two lobes and a point.
            const float lobe = r * 0.46f;
            dl->AddCircleFilled(ImVec2(c.x - lobe * 0.92f, c.y - lobe * 0.42f), lobe, color, 16);
            dl->AddCircleFilled(ImVec2(c.x + lobe * 0.92f, c.y - lobe * 0.42f), lobe, color, 16);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.92f, c.y - r * 0.06f),
                                  ImVec2(c.x + r * 0.92f, c.y - r * 0.06f),
                                  ImVec2(c.x,             c.y + r * 0.92f), color);
            break;
        }
        case Icon::Browse: {
            // Compass: ring plus a needle.
            dl->AddCircle(c, r * 0.92f, color, 20, th);
            dl->AddTriangleFilled(ImVec2(c.x + r * 0.44f, c.y - r * 0.44f),
                                  ImVec2(c.x - r * 0.10f, c.y + r * 0.10f),
                                  ImVec2(c.x + r * 0.10f, c.y + r * 0.30f), color);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.44f, c.y + r * 0.44f),
                                  ImVec2(c.x + r * 0.10f, c.y - r * 0.10f),
                                  ImVec2(c.x - r * 0.10f, c.y - r * 0.30f), color);
            break;
        }
        case Icon::Vods: {
            // Film strip: a frame with perforations down both sides.
            const ImVec2 a(c.x - r * 0.95f, c.y - r * 0.72f);
            const ImVec2 b(c.x + r * 0.95f, c.y + r * 0.72f);
            dl->AddRect(a, b, color, 2.0f, 0, th);
            const float hole = r * 0.15f;
            for (int i = -1; i <= 1; ++i) {
                const float y = c.y + i * r * 0.42f;
                dl->AddRectFilled(ImVec2(a.x + hole * 0.6f, y - hole * 0.5f),
                                  ImVec2(a.x + hole * 1.9f, y + hole * 0.5f), color);
                dl->AddRectFilled(ImVec2(b.x - hole * 1.9f, y - hole * 0.5f),
                                  ImVec2(b.x - hole * 0.6f, y + hole * 0.5f), color);
            }
            break;
        }
        case Icon::Recovery: {
            // Clock face with an arrow sweeping back: restoring the past.
            dl->PathClear();
            dl->PathArcTo(c, r * 0.82f, IM_PI * 0.35f, IM_PI * 1.85f, 24);
            dl->PathStroke(color, ImDrawFlags_None, th);
            // Arrow head at the open end of the arc.
            const ImVec2 tip(c.x + std::cos(IM_PI * 0.35f) * r * 0.82f,
                             c.y + std::sin(IM_PI * 0.35f) * r * 0.82f);
            dl->AddTriangleFilled(ImVec2(tip.x + r * 0.28f, tip.y - r * 0.05f),
                                  ImVec2(tip.x - r * 0.10f, tip.y - r * 0.30f),
                                  ImVec2(tip.x - r * 0.05f, tip.y + r * 0.28f), color);
            // Hands.
            dl->AddLine(c, ImVec2(c.x, c.y - r * 0.44f), color, th);
            dl->AddLine(c, ImVec2(c.x + r * 0.34f, c.y), color, th);
            break;
        }
        case Icon::Downloads: {
            // Arrow pointing into a tray.
            dl->AddLine(ImVec2(c.x, c.y - r * 0.85f), ImVec2(c.x, c.y + r * 0.12f), color, th);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.42f, c.y + r * 0.02f),
                                  ImVec2(c.x + r * 0.42f, c.y + r * 0.02f),
                                  ImVec2(c.x,             c.y + r * 0.52f), color);
            dl->AddLine(ImVec2(c.x - r * 0.80f, c.y + r * 0.78f),
                        ImVec2(c.x + r * 0.80f, c.y + r * 0.78f), color, th);
            break;
        }
        case Icon::Watching: {
            // Monitor with a play mark.
            const ImVec2 a(c.x - r * 0.95f, c.y - r * 0.80f);
            const ImVec2 b(c.x + r * 0.95f, c.y + r * 0.42f);
            dl->AddRect(a, b, color, 2.5f, 0, th);
            dl->AddTriangleFilled(ImVec2(c.x - r * 0.20f, c.y - r * 0.42f),
                                  ImVec2(c.x + r * 0.34f, c.y - r * 0.19f),
                                  ImVec2(c.x - r * 0.20f, c.y + r * 0.04f), color);
            dl->AddLine(ImVec2(c.x - r * 0.45f, c.y + r * 0.82f),
                        ImVec2(c.x + r * 0.45f, c.y + r * 0.82f), color, th);
            break;
        }
        case Icon::Settings: {
            // Gear: ring, hub and radial teeth.
            dl->AddCircle(c, r * 0.52f, color, 18, th);
            for (int i = 0; i < 6; ++i) {
                const float angle = (float)i * IM_PI / 3.0f;
                const float ca = std::cos(angle), sa = std::sin(angle);
                dl->AddLine(ImVec2(c.x + ca * r * 0.60f, c.y + sa * r * 0.60f),
                            ImVec2(c.x + ca * r * 0.95f, c.y + sa * r * 0.95f), color, th);
            }
            break;
        }
        case Icon::Logs: {
            // Ruled lines, like a log file.
            for (int i = -1; i <= 1; ++i) {
                const float y     = c.y + i * r * 0.50f;
                const float right = (i == 1) ? r * 0.30f : r * 0.85f;
                dl->AddLine(ImVec2(c.x - r * 0.85f, y), ImVec2(c.x + right, y), color, th);
            }
            break;
        }
        case Icon::About: {
            dl->AddCircle(c, r * 0.90f, color, 20, th);
            dl->AddCircleFilled(ImVec2(c.x, c.y - r * 0.42f), th * 0.62f, color, 8);
            dl->AddLine(ImVec2(c.x, c.y - r * 0.10f), ImVec2(c.x, c.y + r * 0.48f), color, th);
            break;
        }
    }
}

bool fullscreenButton(const char* id, float width, float height, bool exitIcon) {
    ImGui::PushID(id);

    const bool clicked = ImGui::InvisibleButton("##fs", ImVec2(width, height));
    const bool hovered = ImGui::IsItemHovered();
    const bool held    = ImGui::IsItemActive();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 a = ImGui::GetItemRectMin();
    const ImVec2 b = ImGui::GetItemRectMax();

    dl->AddRectFilled(a, b,
                      held    ? col::kAccentDim
                      : hovered ? col::kCardHover
                                : col::kCard,
                      8.0f);

    const ImVec2 c((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f);
    const float  r     = std::min(width, height) * 0.26f;   // half the glyph box
    const float  arm   = r * 0.62f;                          // bracket arm length
    const float  thick = 1.8f;
    const ImU32  ink   = col::kText;

    // Each corner is an L. Entering points the arms inwards, leaving points
    // them outwards, which is the convention every player uses.
    const float sign = exitIcon ? -1.0f : 1.0f;
    const float inset = exitIcon ? arm : 0.0f;

    struct Corner { float sx, sy; };
    const Corner corners[4] = {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}};

    for (const Corner& corner : corners) {
        const float px = c.x + corner.sx * (r - inset);
        const float py = c.y + corner.sy * (r - inset);
        dl->AddLine(ImVec2(px, py), ImVec2(px - corner.sx * arm * sign, py), ink, thick);
        dl->AddLine(ImVec2(px, py), ImVec2(px, py - corner.sy * arm * sign), ink, thick);
    }

    ImGui::PopID();
    return clicked;
}

void beginCard(const char* id, const ImVec2& size, bool* hovered) {
    ImGui::PushStyleColor(ImGuiCol_ChildBg, toVec4(col::kCard));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14, 12));
    // NoScrollWithMouse lets the wheel fall through to the scrolling parent.
    // Without it a card under the cursor swallows the event and the grid only
    // scrolls when the pointer happens to sit in the gap between cards.
    ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    if (hovered) *hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                   ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
}

void endCard() {
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

void badge(const char* text, ImU32 bg, ImU32 fg) {
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    ImVec2 pos      = ImGui::GetCursorScreenPos();
    ImVec2 textSize = ImGui::CalcTextSize(text);
    ImVec2 pad(8.0f, 3.0f);
    ImVec2 size(textSize.x + pad.x * 2, textSize.y + pad.y * 2);

    dl->AddRectFilled(pos, ImVec2(pos.x + size.x, pos.y + size.y), bg, size.y * 0.5f);
    dl->AddText(ImVec2(pos.x + pad.x, pos.y + pad.y), fg, text);
    ImGui::Dummy(size);
}

void sectionHeader(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::kText));
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float width = ImGui::GetContentRegionAvail().x;
    dl->AddLine(ImVec2(p.x, p.y + 3), ImVec2(p.x + width, p.y + 3), col::kBorder, 1.0f);
    ImGui::Dummy(ImVec2(0, 8));
}

void textEllipsis(const char* text, float maxWidth, ImU32 color) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(color));
    if (ImGui::CalcTextSize(text).x <= maxWidth) {
        ImGui::TextUnformatted(text);
    } else {
        // Binary-search the longest prefix that fits alongside the ellipsis.
        const float ellipsisWidth = ImGui::CalcTextSize("...").x;
        size_t len = std::strlen(text);
        size_t lo = 0, hi = len;
        while (lo < hi) {
            size_t mid = (lo + hi + 1) / 2;
            if (ImGui::CalcTextSize(text, text + mid).x + ellipsisWidth <= maxWidth) lo = mid;
            else hi = mid - 1;
        }
        // Do not cut a UTF-8 sequence in half.
        while (lo > 0 && ((unsigned char)text[lo] & 0xC0) == 0x80) --lo;
        ImGui::TextUnformatted(text, text + lo);
        ImGui::SameLine(0, 0);
        ImGui::TextUnformatted("...");
    }
    ImGui::PopStyleColor();

    if (ImGui::IsItemHovered() && ImGui::CalcTextSize(text).x > maxWidth)
        ImGui::SetTooltip("%s", text);
}

void hint(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, toVec4(col::kTextDim));
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void spinner(float radius, float thickness, ImU32 color) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImVec2 center(pos.x + radius, pos.y + radius);

    const int   segments = 30;
    const float t        = (float)ImGui::GetTime();
    const float start    = std::fmod(t * 3.0f, IM_PI * 2.0f);
    const float arc      = IM_PI * 1.4f;

    dl->PathClear();
    for (int i = 0; i <= segments; ++i) {
        float a = start + (i / (float)segments) * arc;
        dl->PathLineTo(ImVec2(center.x + std::cos(a) * radius,
                              center.y + std::sin(a) * radius));
    }
    dl->PathStroke(color, ImDrawFlags_None, thickness);
    ImGui::Dummy(ImVec2(radius * 2, radius * 2));
}

void progressBar(float fraction, const ImVec2& size, const char* overlay) {
    fraction = ImClamp(fraction, 0.0f, 1.0f);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 pos = ImGui::GetCursorScreenPos();
    float width  = size.x > 0 ? size.x : ImGui::GetContentRegionAvail().x;
    float height = size.y > 0 ? size.y : 18.0f;
    ImVec2 end(pos.x + width, pos.y + height);

    dl->AddRectFilled(pos, end, col::kCard, height * 0.5f);
    if (fraction > 0.001f) {
        dl->AddRectFilled(pos, ImVec2(pos.x + width * fraction, end.y),
                          col::kAccent, height * 0.5f);
    }
    if (overlay && overlay[0]) {
        ImVec2 ts = ImGui::CalcTextSize(overlay);
        dl->AddText(ImVec2(pos.x + (width - ts.x) * 0.5f, pos.y + (height - ts.y) * 0.5f),
                    col::kText, overlay);
    }
    ImGui::Dummy(ImVec2(width, height));
}

bool inputText(const char* id, char* buf, size_t bufSize, const char* placeholder,
               float width, ImGuiInputTextFlags flags) {
    ImGui::PushItemWidth(width);
    bool changed = ImGui::InputTextWithHint(id, placeholder, buf, bufSize, flags);
    ImGui::PopItemWidth();
    return changed;
}

void centerNextItem(float itemWidth) {
    float avail = ImGui::GetContentRegionAvail().x;
    if (avail > itemWidth)
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (avail - itemWidth) * 0.5f);
}

} // namespace dfts::ui
