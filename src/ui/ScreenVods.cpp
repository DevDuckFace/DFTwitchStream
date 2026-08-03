#include "app/App.h"
#include "ui/Theme.h"
#include "ui/Cards.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <algorithm>

namespace dfts {

void App::drawVods() {
    // --- toolbar -----------------------------------------------------------
    ImGui::BeginGroup();
    bool submitted = ui::inputText("##vodChannel", m_vodChannel, sizeof(m_vodChannel),
                                   T("vods.channelInput"), 280.0f,
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ui::primaryButton(T("vods.load"), 150, 0) || submitted)
        loadVodsForChannel(m_vodChannel);

    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    const char* types[] = {"archive", "highlight", "upload", T("common.all")};
    if (ImGui::Combo("##vodType", &m_vodTypeIndex, types, IM_ARRAYSIZE(types))) {
        if (!m_vodChannelLoaded.empty()) loadVodsForChannel(m_vodChannelLoaded);
    }

    ImGui::SameLine();
    if (ui::ghostButton(T("common.refresh"), 120, 0) && !m_vodChannelLoaded.empty())
        loadVodsForChannel(m_vodChannelLoaded);
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 4));
    ImGui::PushFont(m_fonts.small_);
    ui::hint(T("vods.hint"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 8));

    if (!ImGui::BeginTabBar("##vodTabs")) return;

    const bool vodsTab = ImGui::BeginTabItem(T("vods.tabVods"));
    if (vodsTab) {
        ImGui::Dummy(ImVec2(0, 6));
        drawVodsTable();
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(T("vods.tabClips"))) {
        ImGui::Dummy(ImVec2(0, 6));
        drawClipsTab();
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
}

void App::drawClipsTab() {
    // --- range picker ------------------------------------------------------
    ImGui::BeginGroup();
    const char* ranges[] = {T("clips.range24h"), T("clips.range7d"),
                            T("clips.range30d"), T("clips.rangeAll")};
    ImGui::SetNextItemWidth(190.0f);
    if (ImGui::Combo("##clipRange", &m_clipRangeIndex, ranges, IM_ARRAYSIZE(ranges))) {
        if (!m_clipsChannelLoaded.empty()) loadClipsForChannel(m_clipsChannelLoaded);
    }
    ImGui::SameLine();
    if (ui::primaryButton(T("clips.load"), 150, 0)) loadClipsForChannel(m_vodChannel);
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 8));

    if (m_clipsLoading) {
        ui::spinner(11.0f, 3.0f);
        ImGui::SameLine(0, 12);
        ui::hint(T("common.loading"));
        return;
    }
    if (m_clips.empty()) {
        ui::hint(m_clipsChannelLoaded.empty() ? T("vods.channelInput") : T("clips.empty"));
        return;
    }

    ImGui::BeginChild("##clipScroll", ImVec2(0, 0));

    const int   columns   = std::clamp(Config::get().settings().gridColumns, 1, 6);
    const float spacing   = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float cardWidth = (available - spacing * (columns - 1)) / columns;
    const float thumbH    = (cardWidth - 28.0f) * 9.0f / 16.0f;
    const float cardH     = thumbH + 150.0f;

    for (size_t i = 0; i < m_clips.size(); ++i) {
        if (i % columns != 0) ImGui::SameLine();
        const twitch::Clip& clip = m_clips[i];

        const std::string id = "##clip" + clip.id;
        bool hovered = false;
        ui::beginCard(id.c_str(), ImVec2(cardWidth, cardH), &hovered);

        const float inner = ImGui::GetContentRegionAvail().x;
        const ImVec2 thumbPos = ImGui::GetCursorScreenPos();
        ui::thumbnail(clip.thumbnailUrl, ImVec2(inner, thumbH), hovered);

        // Duration and views over the still, same language as the stream cards.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const std::string length = util::formatDuration((int)clip.duration);
        const ImVec2 ls = ImGui::CalcTextSize(length.c_str());
        ImVec2 la(thumbPos.x + inner - ls.x - 22, thumbPos.y + thumbH - ls.y - 14);
        dl->AddRectFilled(la, ImVec2(la.x + ls.x + 14, la.y + ls.y + 6),
                          IM_COL32(0, 0, 0, 170), (ls.y + 6) * 0.5f);
        dl->AddText(ImVec2(la.x + 7, la.y + 3), IM_COL32_WHITE, length.c_str());

        const std::string views = util::formatCount((uint64_t)clip.viewCount);
        const ImVec2 vs = ImGui::CalcTextSize(views.c_str());
        ImVec2 va(thumbPos.x + 8, thumbPos.y + thumbH - vs.y - 14);
        dl->AddRectFilled(va, ImVec2(va.x + vs.x + 14, va.y + vs.y + 6),
                          IM_COL32(0, 0, 0, 170), (vs.y + 6) * 0.5f);
        dl->AddText(ImVec2(va.x + 7, va.y + 3), IM_COL32_WHITE, views.c_str());

        ImGui::Dummy(ImVec2(0, 6));
        ImGui::PushFont(m_fonts.medium);
        ui::textEllipsis(clip.title.c_str(), inner);
        ImGui::PopFont();

        ImGui::PushFont(m_fonts.small_);
        const std::string by = clip.creatorName.empty()
                             ? util::formatLocalDateTime(clip.createdAtUnix)
                             : I18n::get().fmt("clips.clippedBy", clip.creatorName);
        ui::textEllipsis(by.c_str(), inner, ui::col::kTextDim);
        ImGui::PopFont();

        ImGui::SetCursorPosY(cardH - 46.0f);
        const float third = (inner - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;
        if (ui::primaryButton((std::string(T("vods.play")) + id).c_str(), third, 32.0f)) {
            const std::string url = clip.guessMp4Url();
            if (url.empty()) toastError(T("clips.noDownload"));
            else             watchDirectUrl(url, clip.title, "");
        }
        ImGui::SameLine();
        if (ui::ghostButton((std::string(T("vods.download")) + id).c_str(), third, 32.0f))
            downloadClip(clip);
        ImGui::SameLine();
        if (ui::ghostButton((std::string(T("common.copy")) + id).c_str(), third, 32.0f)) {
            util::setClipboard(clip.url);
            toastOk(T("common.copied"));
        }

        ui::endCard();
    }

    ImGui::EndChild();
}

void App::drawVodsTable() {
    if (m_vodsLoading) {
        ui::spinner(11.0f, 3.0f);
        ImGui::SameLine(0, 12);
        ui::hint(T("common.loading"));
        return;
    }

    if (m_vods.empty()) {
        ui::hint(m_vodChannelLoaded.empty() ? T("vods.channelInput") : T("vods.empty"));
        ImGui::Dummy(ImVec2(0, 14));
        if (ui::ghostButton(T("recovery.title"), 220, 38)) {
            if (!m_vodChannelLoaded.empty())
                std::snprintf(m_recStreamer, sizeof(m_recStreamer), "%s", m_vodChannelLoaded.c_str());
            setScreen(Screen::Recovery);
        }
        return;
    }

    // --- table -------------------------------------------------------------
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                            ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable |
                            ImGuiTableFlags_SizingStretchProp;

    if (!ImGui::BeginTable("##vodTable", 6, flags, ImVec2(0, 0))) return;

    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn(T("common.date"),     ImGuiTableColumnFlags_WidthFixed, 150.0f);
    ImGui::TableSetupColumn(T("common.title"),    ImGuiTableColumnFlags_WidthStretch, 3.0f);
    ImGui::TableSetupColumn(T("common.duration"), ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn(T("vods.type"),       ImGuiTableColumnFlags_WidthFixed, 90.0f);
    ImGui::TableSetupColumn(T("common.viewers"),  ImGuiTableColumnFlags_WidthFixed, 80.0f);
    ImGui::TableSetupColumn("",                   ImGuiTableColumnFlags_WidthFixed, 260.0f);
    ImGui::TableHeadersRow();

    for (size_t i = 0; i < m_vods.size(); ++i) {
        const twitch::Vod& item = m_vods[i];
        ImGui::TableNextRow();
        ImGui::PushID((int)i);

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(util::formatLocalDateTime(item.createdAtUnix).c_str());

        ImGui::TableSetColumnIndex(1);
        ImGui::AlignTextToFramePadding();
        ui::textEllipsis(item.title.c_str(), ImGui::GetContentRegionAvail().x);

        ImGui::TableSetColumnIndex(2);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(util::formatDuration(item.durationSecs).c_str());

        ImGui::TableSetColumnIndex(3);
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
        ImGui::TextUnformatted(item.type.c_str());
        ImGui::PopStyleColor();

        ImGui::TableSetColumnIndex(4);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(util::formatCount((uint64_t)item.viewCount).c_str());

        ImGui::TableSetColumnIndex(5);
        if (ui::primaryButton(T("vods.play"), 78, 0)) watchVod(item);
        ImGui::SameLine();
        if (ui::ghostButton(T("vods.download"), 86, 0)) downloadVod(item);
        ImGui::SameLine();
        if (ui::ghostButton(T("vods.copyUrl"), 82, 0)) {
            util::setClipboard(item.url.empty() ? ("https://www.twitch.tv/videos/" + item.id)
                                                : item.url);
            toastOk(T("common.copied"));
        }

        ImGui::PopID();
    }

    ImGui::EndTable();
}

} // namespace dfts
