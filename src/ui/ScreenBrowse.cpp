#include "app/App.h"
#include "ui/Theme.h"
#include "ui/TextureCache.h"
#include "ui/Cards.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <algorithm>

namespace dfts {

namespace {

// Grid of category cards. Returns the index that was clicked, or -1.
int drawCategoryGrid(const char* idPrefix, const std::vector<twitch::Game>& games,
                     const ui::FontSet& fonts, int columns) {
    int clicked = -1;
    const float spacing   = ImGui::GetStyle().ItemSpacing.x;
    const float available = ImGui::GetContentRegionAvail().x;
    const float cardWidth = (available - spacing * (columns - 1)) / columns;
    // Twitch box art is 3:4, so the artwork drives the card height.
    const float artHeight  = cardWidth * 1.33f;
    const float cardHeight = artHeight + 54.0f;

    for (size_t i = 0; i < games.size(); ++i) {
        if (i % columns != 0) ImGui::SameLine();
        const twitch::Game& game = games[i];

        std::string id = std::string("##") + idPrefix + game.id + std::to_string(i);
        bool hovered = false;
        ui::beginCard(id.c_str(), ImVec2(cardWidth, cardHeight), &hovered);

        float inner = ImGui::GetContentRegionAvail().x;
        ui::thumbnail(twitch::sizedThumb(game.boxArtUrl, 285, 380),
                      ImVec2(inner, artHeight), hovered);

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::PushFont(fonts.medium);
        ui::textEllipsis(game.name.c_str(), inner);
        ImGui::PopFont();

        // The whole card is clickable, not just a button.
        ImGui::SetCursorPos(ImVec2(0, 0));
        if (ImGui::InvisibleButton((id + "hit").c_str(), ImVec2(inner, cardHeight - 8.0f)))
            clicked = (int)i;

        ui::endCard();
    }
    return clicked;
}

} // namespace

void App::drawBrowse() {
    // --- search bar --------------------------------------------------------
    ImGui::BeginGroup();
    float searchWidth = std::min(460.0f, ImGui::GetContentRegionAvail().x - 320.0f);
    bool submitted = ui::inputText("##browseQuery", m_browseQuery, sizeof(m_browseQuery),
                                   T("browse.searchHint"), searchWidth,
                                   ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    bool clicked = ui::primaryButton(T("common.search"), 120, 0);
    ImGui::SameLine();
    if (ui::ghostButton(T("common.refresh"), 120, 0)) {
        m_browseLoaded = false;
        refreshBrowse();
    }

    // --- language filter ---------------------------------------------------
    // Codes Twitch expects on the streams endpoints; "" is every language.
    static const char* kLangCodes[]  = {"", "pt", "en", "es", "fr", "de", "it", "ja", "ko", "ru"};
    static const char* kLangLabels[] = {"Todos / All", "Português", "English", "Español",
                                        "Français", "Deutsch", "Italiano",
                                        "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E",
                                        "\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4",
                                        "\xD0\xA0\xD1\x83\xD1\x81\xD1\x81\xD0\xBA\xD0\xB8\xD0\xB9"};

    Settings& browseSettings = Config::get().settings();
    int langIndex = 0;
    for (int i = 0; i < IM_ARRAYSIZE(kLangCodes); ++i)
        if (browseSettings.browseLanguage == kLangCodes[i]) langIndex = i;

    ImGui::SameLine();
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::Combo("##browseLang", &langIndex, kLangLabels, IM_ARRAYSIZE(kLangLabels))) {
        browseSettings.browseLanguage = kLangCodes[langIndex];
        Config::get().save();
        // Re-query with the new filter, keeping whatever view is open.
        if (!m_openCategory.id.empty())      openCategory(m_openCategory);
        else if (m_searchActive)             runSearch(m_browseQuery);
        else { m_browseLoaded = false; refreshBrowse(); }
    }

    if (m_searchActive || !m_openCategory.id.empty()) {
        ImGui::SameLine();
        if (ui::ghostButton(T("browse.backToTop"), 150, 0)) {
            m_searchActive = false;
            m_openCategory = {};
            m_browseQuery[0] = '\0';
            m_searchGames.clear();
            m_searchStreams.clear();
            m_categoryStreams.clear();
        }
    }
    ImGui::EndGroup();

    if (submitted || clicked) {
        std::string query = util::trim(m_browseQuery);
        if (!query.empty()) runSearch(query);
    }

    // Load the top lists the first time this screen is opened.
    if (!m_browseLoaded && !m_browseLoading) refreshBrowse();

    ImGui::Dummy(ImVec2(0, 10));

    const int columns = std::clamp(Config::get().settings().gridColumns, 1, 6);

    // --- drilled into one category -----------------------------------------
    if (!m_openCategory.id.empty()) {
        auto leaveCategory = [&]() {
            m_openCategory = {};
            m_categoryStreams.clear();
        };

        // The category title and the way out live above the scrolling area, so
        // the button stays put however far down the list you are. Inside the
        // scroll region it simply drifted off the top.
        ImGui::PushFont(m_fonts.large);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(m_openCategory.name.c_str());
        ImGui::PopFont();

        ImGui::SameLine(0, 20);
        ImGui::PushFont(m_fonts.large);
        const bool goBack = ui::alertButton(T("browse.back"), 170.0f, 40.0f);
        ImGui::PopFont();

        if (!m_categoryLoading && !m_categoryStreams.empty()) {
            ImGui::SameLine(0, 20);
            ImGui::PushFont(m_fonts.small_);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
            ImGui::TextUnformatted(I18n::get().fmt("browse.liveInCategory",
                                                   m_openCategory.name).c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        if (goBack) { leaveCategory(); return; }
        ImGui::Dummy(ImVec2(0, 10));

        if (m_categoryLoading) {
            ui::spinner(11.0f, 3.0f);
            ImGui::SameLine(0, 12);
            ui::hint(T("common.loading"));
            return;
        }
        if (m_categoryStreams.empty()) {
            ui::hint(T("browse.noLiveInCategory"));
            return;
        }

        ImGui::BeginChild("##categoryScroll", ImVec2(0, 0));
        if (m_categoryScrollReset) {
            ImGui::SetScrollY(0.0f);
            m_categoryScrollReset = false;
        }
        ui::StreamGridAction action = ui::drawStreamGrid("cat", m_categoryStreams,
                                                         m_fonts, columns);
        ImGui::EndChild();

        if (action.watchIndex >= 0) {
            const auto& s = m_categoryStreams[action.watchIndex];
            watchChannel(s.userLogin, s.userName);
        } else if (action.vodsIndex >= 0) {
            const auto& s = m_categoryStreams[action.vodsIndex];
            std::snprintf(m_vodChannel, sizeof(m_vodChannel), "%s", s.userLogin.c_str());
            setScreen(Screen::Vods);
            loadVodsForChannel(s.userLogin);
        }
        return;
    }

    if (m_browseLoading) {
        ui::spinner(11.0f, 3.0f);
        ImGui::SameLine(0, 12);
        ui::hint(T("common.loading"));
        return;
    }

    // --- search results, or the top lists ----------------------------------
    const bool showingSearch = m_searchActive;
    const std::vector<twitch::Game>&   games   = showingSearch ? m_searchGames   : m_topGames;
    const std::vector<twitch::Stream>& streams = showingSearch ? m_searchStreams : m_topStreams;

    if (!ImGui::BeginTabBar("##browseTabs")) return;

    // --- categories tab ---
    std::string catLabel = std::string(showingSearch ? T("browse.matchingCategories")
                                                     : T("browse.topCategories")) +
                           " (" + std::to_string(games.size()) + ")###browseCats";
    if (ImGui::BeginTabItem(catLabel.c_str())) {
        m_browseTab = 0;
        ImGui::Dummy(ImVec2(0, 8));

        if (games.empty()) {
            ui::hint(showingSearch ? T("browse.noCategoryMatch") : T("common.none"));
        } else {
            ImGui::BeginChild("##catScroll", ImVec2(0, 0));
            if (m_browseScrollReset) ImGui::SetScrollY(0.0f);
            int picked = drawCategoryGrid(showingSearch ? "sc" : "tc", games, m_fonts,
                                          std::min(columns + 2, 8));
            ImGui::EndChild();
            if (picked >= 0) openCategory(games[picked]);
        }
        ImGui::EndTabItem();
    }

    // --- channels tab ---
    std::string chanLabel = std::string(showingSearch ? T("browse.matchingChannels")
                                                      : T("browse.topChannels")) +
                            " (" + std::to_string(streams.size()) + ")###browseChans";
    if (ImGui::BeginTabItem(chanLabel.c_str())) {
        m_browseTab = 1;
        ImGui::Dummy(ImVec2(0, 8));

        if (streams.empty()) {
            ui::hint(T("common.none"));
        } else {
            ImGui::BeginChild("##chanScroll", ImVec2(0, 0));
            if (m_browseScrollReset) ImGui::SetScrollY(0.0f);
            ui::StreamGridAction action = ui::drawStreamGrid(showingSearch ? "ss" : "ts",
                                                             streams, m_fonts, columns);
            ImGui::EndChild();

            if (action.watchIndex >= 0) {
                const auto& s = streams[action.watchIndex];
                watchChannel(s.userLogin, s.userName);
            } else if (action.vodsIndex >= 0) {
                const auto& s = streams[action.vodsIndex];
                std::snprintf(m_vodChannel, sizeof(m_vodChannel), "%s", s.userLogin.c_str());
                setScreen(Screen::Vods);
                loadVodsForChannel(s.userLogin);
            }
        }
        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    // Cleared once both tabs have had their chance this frame; only the
    // visible one actually consumes it.
    m_browseScrollReset = false;
}

} // namespace dfts
