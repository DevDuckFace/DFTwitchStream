#include "ui/Cards.h"
#include "ui/Theme.h"
#include "ui/TextureCache.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <algorithm>

namespace dfts::ui {

void thumbnail(const std::string& url, const ImVec2& size, bool hovered) {
    ImVec2 pos = ImGui::GetCursorScreenPos();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 end(pos.x + size.x, pos.y + size.y);
    const float rounding = 8.0f;

    int texW = 0, texH = 0;
    ImTextureID tex = TextureCache::get().texture(url, &texW, &texH);

    if (tex && texW > 0 && texH > 0) {
        // Crop to fill: scale to cover the box, then centre the overflow.
        float boxAspect = size.x / std::max(1.0f, size.y);
        float imgAspect = (float)texW / (float)texH;
        ImVec2 uv0(0, 0), uv1(1, 1);
        if (imgAspect > boxAspect) {
            float visible = boxAspect / imgAspect;
            float inset   = (1.0f - visible) * 0.5f;
            uv0.x = inset;
            uv1.x = 1.0f - inset;
        } else if (imgAspect < boxAspect) {
            float visible = imgAspect / boxAspect;
            float inset   = (1.0f - visible) * 0.5f;
            uv0.y = inset;
            uv1.y = 1.0f - inset;
        }
        ImU32 tint = hovered ? IM_COL32(255, 255, 255, 255) : IM_COL32(228, 228, 232, 255);
        dl->AddImageRounded(tex, pos, end, uv0, uv1, tint, rounding);
    } else {
        // Placeholder: a flat panel so the layout does not jump when the image
        // finishes downloading.
        dl->AddRectFilled(pos, end, col::kPanel, rounding);
        const char* dots = "...";
        ImVec2 ts = ImGui::CalcTextSize(dots);
        dl->AddText(ImVec2(pos.x + (size.x - ts.x) * 0.5f, pos.y + (size.y - ts.y) * 0.5f),
                    col::kTextDim, dots);
    }

    ImGui::Dummy(size);
}

StreamGridAction drawStreamGrid(const char* idPrefix,
                                const std::vector<twitch::Stream>& streams,
                                const FontSet& fonts, int columns) {
    StreamGridAction action;
    columns = std::clamp(columns, 1, 8);

    const float spacing   = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float cardWidth = (available - spacing * (columns - 1)) / columns;
    // Stream previews are 16:9; the text block below it has a fixed height.
    const float thumbHeight = (cardWidth - 28.0f) * 9.0f / 16.0f;
    const float cardHeight  = thumbHeight + 158.0f;

    for (size_t i = 0; i < streams.size(); ++i) {
        if (i % columns != 0) ImGui::SameLine();
        const twitch::Stream& stream = streams[i];

        std::string id = std::string("##") + idPrefix + stream.userId + std::to_string(i);
        bool hovered = false;
        beginCard(id.c_str(), ImVec2(cardWidth, cardHeight), &hovered);

        const float inner = ImGui::GetContentRegionAvail().x;

        // --- preview image with overlays ---
        ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        thumbnail(twitch::sizedThumb(stream.thumbnailUrl, 440, 248),
                  ImVec2(inner, thumbHeight), hovered);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        const bool isLive = stream.type.empty() || stream.type == "live";

        if (isLive) {
            // LIVE pill, top-left over the image.
            const char* liveText = T("common.live");
            ImVec2 ts = ImGui::CalcTextSize(liveText);
            ImVec2 a(thumbPos.x + 8, thumbPos.y + 8);
            ImVec2 b(a.x + ts.x + 14, a.y + ts.y + 6);
            dl->AddRectFilled(a, b, col::kLive, (b.y - a.y) * 0.5f);
            dl->AddText(ImVec2(a.x + 7, a.y + 3), IM_COL32_WHITE, liveText);

            // Viewer count, bottom-left over the image.
            std::string viewers = util::formatCount((uint64_t)stream.viewerCount) + " " +
                                  T("common.viewers");
            ImVec2 vs = ImGui::CalcTextSize(viewers.c_str());
            ImVec2 c(thumbPos.x + 8, thumbPos.y + thumbHeight - vs.y - 14);
            ImVec2 d(c.x + vs.x + 14, c.y + vs.y + 6);
            dl->AddRectFilled(c, d, IM_COL32(0, 0, 0, 170), (d.y - c.y) * 0.5f);
            dl->AddText(ImVec2(c.x + 7, c.y + 3), IM_COL32_WHITE, viewers.c_str());
        }

        // Uptime, bottom-right over the image.
        if (stream.startedAtUnix > 0 && isLive) {
            int uptime = (int)(util::unixNow() - stream.startedAtUnix);
            std::string text = util::formatDuration(uptime);
            ImVec2 ts = ImGui::CalcTextSize(text.c_str());
            ImVec2 a(thumbPos.x + inner - ts.x - 22, thumbPos.y + thumbHeight - ts.y - 14);
            ImVec2 b(a.x + ts.x + 14, a.y + ts.y + 6);
            dl->AddRectFilled(a, b, IM_COL32(0, 0, 0, 170), (b.y - a.y) * 0.5f);
            dl->AddText(ImVec2(a.x + 7, a.y + 3), IM_COL32_WHITE, text.c_str());
        }

        // --- text block ---
        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushFont(fonts.medium);
        textEllipsis(stream.userName.c_str(), inner);
        ImGui::PopFont();

        ImGui::PushFont(fonts.small_);
        textEllipsis(stream.title.c_str(), inner, col::kTextDim);
        if (!stream.gameName.empty())
            textEllipsis(stream.gameName.c_str(), inner, col::kAccentHover);
        ImGui::PopFont();

        // --- actions ---
        ImGui::SetCursorPosY(cardHeight - 46.0f);
        float half = (inner - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (primaryButton((std::string(T("following.watch")) + id).c_str(), half, 32.0f))
            action.watchIndex = (int)i;
        ImGui::SameLine();
        if (ghostButton((std::string(T("following.vods")) + id).c_str(), half, 32.0f))
            action.vodsIndex = (int)i;

        endCard();
    }

    return action;
}

} // namespace dfts::ui
