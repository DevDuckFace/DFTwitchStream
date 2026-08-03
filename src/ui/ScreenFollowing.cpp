#include "app/App.h"
#include "ui/Theme.h"
#include "ui/Cards.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <algorithm>
#include <set>

namespace dfts {

void App::drawFollowing() {
    if (!Config::get().settings().loggedIn()) {
        ImGui::Dummy(ImVec2(0, 40));
        ui::hint(T("following.notLoggedIn"));
        ImGui::Dummy(ImVec2(0, 12));
        if (ui::primaryButton(T("login.start"), 220, 40)) setScreen(Screen::Login);
        return;
    }

    // --- toolbar -----------------------------------------------------------
    ImGui::BeginGroup();
    ui::inputText("##followFilter", m_followFilter, sizeof(m_followFilter),
                  T("following.filter"), 320.0f);
    ImGui::SameLine();
    if (ui::ghostButton(T("common.refresh"), 120, 0)) refreshFollowing();
    ImGui::SameLine();

    bool& showOffline = Config::get().settings().showOfflineChannels;
    if (ImGui::Checkbox(T("settings.showOffline"), &showOffline)) Config::get().save();
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 10));

    if (m_followingLoading && m_liveStreams.empty()) {
        ui::spinner(11.0f, 3.0f);
        ImGui::SameLine(0, 12);
        ui::hint(T("common.loading"));
        return;
    }

    const std::string filter = util::trim(m_followFilter);

    // --- live section ------------------------------------------------------
    std::vector<twitch::Stream> visible;
    for (const auto& s : m_liveStreams) {
        if (filter.empty() || util::icontains(s.userName, filter) ||
            util::icontains(s.userLogin, filter) || util::icontains(s.title, filter) ||
            util::icontains(s.gameName, filter))
            visible.push_back(s);
    }

    ImGui::BeginChild("##followScroll", ImVec2(0, 0), ImGuiChildFlags_None);

    ui::sectionHeader(T("following.liveNow"));

    if (visible.empty()) {
        ui::hint(m_liveStreams.empty() ? T("following.empty") : T("common.none"));
    } else {
        const int columns = std::clamp(Config::get().settings().gridColumns, 1, 6);
        ui::StreamGridAction action = ui::drawStreamGrid("fw", visible, m_fonts, columns);

        if (action.watchIndex >= 0) {
            const auto& s = visible[action.watchIndex];
            watchChannel(s.userLogin, s.userName);
        } else if (action.vodsIndex >= 0) {
            const auto& s = visible[action.vodsIndex];
            std::snprintf(m_vodChannel, sizeof(m_vodChannel), "%s", s.userLogin.c_str());
            setScreen(Screen::Vods);
            loadVodsForChannel(s.userLogin);
        }
    }

    // --- offline section ---------------------------------------------------
    if (showOffline) {
        ImGui::Dummy(ImVec2(0, 18));
        ui::sectionHeader(T("following.offline"));

        std::set<std::string> liveIds;
        for (const auto& s : m_liveStreams) liveIds.insert(s.userId);

        std::vector<const twitch::FollowedChannel*> offline;
        for (const auto& f : m_followed) {
            if (liveIds.count(f.userId)) continue;
            if (!filter.empty() && !util::icontains(f.displayName, filter) &&
                !util::icontains(f.login, filter))
                continue;
            offline.push_back(&f);
        }

        if (offline.empty()) {
            ui::hint(T("common.none"));
        } else {
            const int   columns   = std::clamp(Config::get().settings().gridColumns + 1, 2, 7);
            const float spacing   = ImGui::GetStyle().ItemSpacing.x;
            const float available = ImGui::GetContentRegionAvail().x;
            const float cardWidth = (available - spacing * (columns - 1)) / columns;

            for (size_t i = 0; i < offline.size(); ++i) {
                if (i % columns != 0) ImGui::SameLine();
                const twitch::FollowedChannel& channel = *offline[i];

                std::string id = "##off" + channel.userId;
                ui::beginCard(id.c_str(), ImVec2(cardWidth, 96.0f));

                float inner = ImGui::GetContentRegionAvail().x;
                ImGui::PushFont(m_fonts.medium);
                ui::textEllipsis(channel.displayName.c_str(), inner);
                ImGui::PopFont();

                ImGui::PushFont(m_fonts.small_);
                ui::textEllipsis(T("common.offline"), inner, ui::col::kTextDim);
                ImGui::PopFont();

                ImGui::SetCursorPosY(96.0f - 44.0f);
                if (ui::ghostButton((std::string(T("following.vods")) + id).c_str(), -1, 30.0f)) {
                    std::snprintf(m_vodChannel, sizeof(m_vodChannel), "%s", channel.login.c_str());
                    setScreen(Screen::Vods);
                    loadVodsForChannel(channel.login);
                }
                ui::endCard();
            }
        }
    }

    ImGui::EndChild();
}

} // namespace dfts
