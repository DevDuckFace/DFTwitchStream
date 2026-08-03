#include "app/App.h"
#include "ui/Theme.h"
#include "core/Util.h"
#include "i18n/I18n.h"
#include "chat/ChatClient.h"

#include "ui/TextureCache.h"

#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace dfts {

namespace {

using BadgeResolver = std::function<std::string(const std::string&, const std::string&)>;
// Looks a plain word up in the 7TV/BTTV/FFZ tables; "" means it is just text.
using WordEmoteResolver = std::function<std::string(const std::string&)>;

// One piece of a message: either a run of text or an emote image.
struct Token {
    bool        emote = false;
    std::string text;    // word, when emote == false
    std::string url;     // image, when emote == true
};

// Splits the message into words and emote images, keeping their order.
//
// Twitch's own emotes arrive as byte ranges in the IRC tag. Community emotes
// carry no such marker, so every remaining word is looked up by name -- which
// is exactly how the browser extensions do it.
std::vector<Token> tokenizeMessage(const chat::Message& m,
                                   const WordEmoteResolver& wordEmote) {
    std::vector<Token> tokens;

    auto pushWord = [&](const std::string& word) {
        if (word.empty()) return;
        std::string url = wordEmote ? wordEmote(word) : std::string();
        if (url.empty()) tokens.push_back({false, word, {}});
        else             tokens.push_back({true, word, std::move(url)});
    };

    auto pushWords = [&](const std::string& chunk) {
        std::string word;
        for (char c : chunk) {
            if (c == ' ' || c == '\t') { pushWord(word); word.clear(); }
            else                       { word += c; }
        }
        pushWord(word);
    };

    size_t cursor = 0;
    for (const chat::EmoteRun& run : m.emotes) {
        if (run.begin > m.text.size() || run.end > m.text.size()) continue;
        if (run.begin > cursor) pushWords(m.text.substr(cursor, run.begin - cursor));
        tokens.push_back({true, m.text.substr(run.begin, run.end - run.begin), run.url("1.0")});
        cursor = run.end;
    }
    if (cursor < m.text.size()) pushWords(m.text.substr(cursor));
    return tokens;
}

// Renders the chat log. Kept separate so the video pane stays readable.
void drawChatMessages(const std::vector<chat::Message>& messages, const ui::FontSet& fonts,
                      bool showTimestamps, float fontScale, bool& scrollToBottom,
                      const BadgeResolver& badgeUrl, const WordEmoteResolver& wordEmote) {
    ImGui::PushFont(fonts.small_);
    if (fontScale != 1.0f) ImGui::SetWindowFontScale(fontScale);

    // No ImGuiListClipper here: it assumes every row has the same height, and
    // wrapped chat lines do not. Feeding it variable heights made the list jump
    // and re-measure constantly, which is what looked like the chat "freezing".
    // The buffer is capped, so drawing it outright is both cheaper and stable.
    {
        for (int i = 0; i < (int)messages.size(); ++i) {
            const chat::Message& m = messages[i];

            ImGui::PushID(i);
            ImGui::PushTextWrapPos(ImGui::GetContentRegionAvail().x);

            switch (m.kind) {
                case chat::MessageKind::System:
                case chat::MessageKind::Notice:
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
                    ImGui::TextWrapped("%s", m.text.c_str());
                    ImGui::PopStyleColor();
                    break;

                case chat::MessageKind::Error:
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImGui::ColorConvertU32ToFloat4(ui::col::kError));
                    ImGui::TextWrapped("%s", m.text.c_str());
                    ImGui::PopStyleColor();
                    break;

                case chat::MessageKind::SubEvent:
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImGui::ColorConvertU32ToFloat4(ui::col::kAccentHover));
                    ImGui::TextWrapped("%s", m.text.c_str());
                    ImGui::PopStyleColor();
                    break;

                case chat::MessageKind::Chat:
                case chat::MessageKind::Action: {
                    // Timestamp, then the coloured name, then the text, all on
                    // one wrapped paragraph.
                    if (showTimestamps) {
                        std::string stamp = util::formatLocalDateTime(m.time);
                        // Keep only HH:MM from "dd/mm/yyyy HH:MM".
                        size_t space = stamp.find(' ');
                        if (space != std::string::npos) stamp = stamp.substr(space + 1);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
                        ImGui::TextUnformatted(stamp.c_str());
                        ImGui::PopStyleColor();
                        ImGui::SameLine(0, 6);
                    }

                    // Real badge artwork when Twitch gave us a URL for it;
                    // otherwise the old coloured pill, so nothing disappears
                    // while the badge lists are still loading.
                    const float lineHeight = ImGui::GetTextLineHeight();
                    bool drewBadge = false;
                    for (const chat::BadgeRef& ref : m.badges) {
                        const std::string url = badgeUrl ? badgeUrl(ref.setId, ref.version)
                                                         : std::string();
                        if (url.empty()) continue;
                        if (ImTextureID tex = ui::TextureCache::get().texture(url)) {
                            ImGui::Image(tex, ImVec2(lineHeight, lineHeight));
                            ImGui::SameLine(0, 3);
                            drewBadge = true;
                        }
                    }
                    if (!drewBadge) {
                        if (m.moderator) {
                            ui::badge("MOD", ui::col::kOk);
                            ImGui::SameLine(0, 5);
                        } else if (m.vip) {
                            ui::badge("VIP", IM_COL32(224, 0, 128, 255));
                            ImGui::SameLine(0, 5);
                        } else if (m.subscriber) {
                            ui::badge("SUB", ui::col::kAccentDim);
                            ImGui::SameLine(0, 5);
                        }
                    }

                    ImU32 nameColor = 0xFF000000u | ((m.color & 0xFF) << 16) |
                                      (m.color & 0xFF00) | ((m.color >> 16) & 0xFF);
                    ImGui::PushStyleColor(ImGuiCol_Text,
                                          ImGui::ColorConvertU32ToFloat4(nameColor));
                    ImGui::TextUnformatted(m.user.c_str());
                    ImGui::PopStyleColor();

                    ImGui::SameLine(0, 0);
                    ImGui::TextUnformatted(m.kind == chat::MessageKind::Action ? " " : ": ");

                    // Words and emotes share one flow. ImGui cannot wrap mixed
                    // text and images on its own, so the line break is decided
                    // per item against the right edge of the chat pane.
                    // Cursor + remaining width is the content's right edge in
                    // screen space, whatever the cursor happens to be doing.
                    const float rightEdge = ImGui::GetCursorScreenPos().x +
                                            ImGui::GetContentRegionAvail().x;
                    const float spaceWidth = ImGui::CalcTextSize(" ").x;
                    const std::vector<Token> tokens = tokenizeMessage(m, wordEmote);

                    for (const Token& token : tokens) {
                        float width = 0.0f;
                        int   texW = 0, texH = 0;
                        ImTextureID tex = 0;

                        if (token.emote) {
                            tex = ui::TextureCache::get().texture(token.url, &texW, &texH);
                            const float aspect = (tex && texH > 0)
                                               ? (float)texW / (float)texH : 1.0f;
                            width = lineHeight * 1.45f * aspect;
                        } else {
                            width = ImGui::CalcTextSize(token.text.c_str()).x;
                        }

                        // Stay on this line while there is room for the item.
                        const float nextRight = ImGui::GetItemRectMax().x + spaceWidth + width;
                        if (nextRight < rightEdge) ImGui::SameLine(0, spaceWidth);

                        if (token.emote && tex) {
                            ImGui::Image(tex, ImVec2(width, lineHeight * 1.45f));
                            if (ImGui::IsItemHovered() && !token.text.empty())
                                ImGui::SetTooltip("%s", token.text.c_str());
                        } else if (token.emote) {
                            ImGui::TextUnformatted(token.text.c_str());   // still loading
                        } else if (m.kind == chat::MessageKind::Action) {
                            ImGui::PushStyleColor(ImGuiCol_Text,
                                                  ImGui::ColorConvertU32ToFloat4(nameColor));
                            ImGui::TextUnformatted(token.text.c_str());
                            ImGui::PopStyleColor();
                        } else {
                            ImGui::TextUnformatted(token.text.c_str());
                        }
                    }
                    break;
                }
            }

            ImGui::PopTextWrapPos();
            ImGui::PopID();
        }
    }

    // Following is sticky. Comparing the scroll position against the maximum
    // after new messages were submitted always fails -- the content just grew,
    // so the view is no longer "at the bottom" -- which silently switched
    // auto-scroll off for the rest of the session.
    //
    // Instead: scrolling up with the wheel stops following, and reaching the
    // bottom again resumes it.
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
    if (hovered && ImGui::GetIO().MouseWheel > 0.0f) scrollToBottom = false;
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) scrollToBottom = true;

    if (scrollToBottom) ImGui::SetScrollY(ImGui::GetScrollMaxY());

    if (fontScale != 1.0f) ImGui::SetWindowFontScale(1.0f);
    ImGui::PopFont();
}

} // namespace

void App::drawWatch() {
    Settings& s = Config::get().settings();

    // --- nothing playing ---------------------------------------------------
    if (m_watchTitle.empty() && !m_watchLoading) {
        playerSetVisible(false);
        ImGui::Dummy(ImVec2(0, 40));
        ui::hint(T("watch.nothing"));
        ImGui::Dummy(ImVec2(0, 14));
        if (ui::primaryButton(T("nav.following"), 220, 40)) setScreen(Screen::Following);
        return;
    }

    const bool isFullscreen = fullscreen();

    // --- header bar (hidden in fullscreen) ---------------------------------
    if (!isFullscreen) {
    ImGui::BeginGroup();
    ImGui::PushFont(m_fonts.medium);
    ui::textEllipsis(m_watchTitle.c_str(), ImGui::GetContentRegionAvail().x - 520.0f);
    ImGui::PopFont();

    // Stream title and category, straight from the channel record.
    if (m_channelInfoValid && !m_channelInfo.title.empty()) {
        ImGui::PushFont(m_fonts.small_);
        std::string line = m_channelInfo.title;
        if (!m_channelInfo.gameName.empty()) line += "  -  " + m_channelInfo.gameName;
        ui::textEllipsis(line.c_str(), ImGui::GetContentRegionAvail().x - 520.0f,
                         ui::col::kTextDim);
        ImGui::PopFont();
    }

    // Quality and volume live in the control bar under the video, not here.
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 340.0f);

    if (ui::ghostButton(T("watch.reload"), 100, 0)) {
        // A manual reload also clears a watchdog that has given up.
        m_watchRetries      = 0;
        m_watchGaveUp       = false;
        m_watchReconnecting = false;
        m_nextRetryAt       = 0.0;
        m_shortSessions = 0;
        m_watchAdLoop   = false;
        if (!m_watchChannel.empty() && m_watchIsLive) reconnectLive();
        else if (m_playlist.ok)                       applyQuality(m_watchSubtitle);
    }
    ImGui::SameLine();
    if (ui::ghostButton(T("watch.toggleChat"), 130, 0)) {
        s.chatEnabled = !s.chatEnabled;
        Config::get().save();
        if (s.chatEnabled && !m_watchChannel.empty())
            m_chat.join(m_watchChannel, s.userLogin, s.accessToken);
        else
            m_chat.leave();
    }
    ImGui::SameLine();
    if (ui::dangerButton(T("watch.stop"), 90, 0)) {
        stopWatching();
        return;
    }
    ImGui::EndGroup();
    ImGui::Dummy(ImVec2(0, 8));
    }   // !isFullscreen

    // --- video + chat split -------------------------------------------------
    // Fullscreen is the player alone; chat would fight the video for the
    // screen and the overlay clip only reserves one rectangle.
    const bool  chatVisible = !isFullscreen && s.chatEnabled && !m_watchChannel.empty();
    const float chatWidth   = chatVisible ? (float)std::clamp(s.chatWidth, 240, 600) : 0.0f;
    const float spacing     = chatVisible ? ImGui::GetStyle().ItemSpacing.x : 0.0f;
    const ImVec2 avail      = ImGui::GetContentRegionAvail();
    const float videoWidth  = std::max(160.0f, avail.x - chatWidth - spacing);
    const float paneHeight  = std::max(160.0f, avail.y - 4.0f);

    const bool chatFirst = chatVisible && !s.chatOnRight;

    auto drawChatPane = [&]() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kPanel));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::BeginChild("##chatPane", ImVec2(chatWidth, paneHeight),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar);

        // Header
        ImGui::PushFont(m_fonts.medium);
        ImGui::TextUnformatted(T("chat.title"));
        ImGui::PopFont();
        ImGui::SameLine();
        ImGui::PushFont(m_fonts.small_);
        if (m_chat.connected())
            ui::badge(("#" + m_chat.channel()).c_str(), ui::col::kAccentDim);
        else
            ui::badge(T("chat.connecting"), ui::col::kBorder);
        ImGui::PopFont();

        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 44.0f);
        ImGui::PushFont(m_fonts.small_);
        if (ui::ghostButton("x##chatClear", 34, 22)) m_chat.clear();
        ImGui::PopFont();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("chat.clear"));

        // --- room rules, so a rejected message has a visible explanation ----
        if (m_chatSettingsValid) {
            const twitch::ChatSettings& cs = m_chatSettings;
            ImGui::PushFont(m_fonts.small_);
            bool any = false;
            auto pill = [&](const std::string& text, ImU32 colour) {
                if (any) ImGui::SameLine(0, 4);
                ui::badge(text.c_str(), colour);
                any = true;
            };
            if (cs.subscriberMode) pill(T("chat.subOnly"), ui::col::kAccentDim);
            if (cs.followerMode)
                pill(cs.followerMinutes > 0
                         ? I18n::get().fmt("chat.followersFor",
                                           std::to_string(cs.followerMinutes))
                         : std::string(T("chat.followersOnly")),
                     ui::col::kAccentDim);
            if (cs.slowMode)
                pill(I18n::get().fmt("chat.slow", std::to_string(cs.slowModeWait)),
                     ui::col::kWarn);
            if (cs.emoteMode)  pill(T("chat.emoteOnly"), ui::col::kWarn);
            if (cs.uniqueMode) pill(T("chat.uniqueOnly"), ui::col::kWarn);
            ImGui::PopFont();
            if (any) ImGui::Dummy(ImVec2(0, 2));
        }

        ImGui::Separator();

        // Message log
        const float inputHeight = 40.0f;
        ImGui::BeginChild("##chatLog", ImVec2(0, ImGui::GetContentRegionAvail().y - inputHeight),
                          ImGuiChildFlags_None);
        // Only re-copy when the client says something actually changed.
        const uint64_t chatVersion = m_chat.version();
        if (chatVersion != m_chatCacheVersion) {
            m_chatCache        = m_chat.messages();
            m_chatCacheVersion = chatVersion;
        }
        drawChatMessages(m_chatCache, m_fonts, s.chatTimestamps,
                         std::clamp(s.chatFontScale, 70, 160) / 100.0f, m_chatScrollToBottom,
                         [this](const std::string& set, const std::string& version) {
                             return badgeUrl(set, version);
                         },
                         [this](const std::string& word) {
                             return thirdPartyEmoteUrl(word);
                         });
        ImGui::EndChild();

        // Composer
        bool canSend = m_chat.canSend();
        ImGui::BeginDisabled(!canSend);
        float sendWidth = 70.0f;
        bool submitted = ui::inputText("##chatInput", m_chatInput, sizeof(m_chatInput),
                                       canSend ? T("chat.sendPlaceholder") : T("chat.readOnly"),
                                       ImGui::GetContentRegionAvail().x - sendWidth -
                                           ImGui::GetStyle().ItemSpacing.x,
                                       ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine();
        bool clicked = ui::primaryButton(">##chatSend", sendWidth, 0);
        ImGui::EndDisabled();

        if (canSend && (submitted || clicked) && m_chatInput[0] != '\0') {
            m_chat.send(m_chatInput);
            m_chatInput[0] = '\0';
            if (submitted) ImGui::SetKeyboardFocusHere(-1);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    };

    // Seeking is a VLC capability and only makes sense for finite media, so
    // live streams get the compact one-row bar.
    const bool  canSeek = (s.playerBackend == 0) && m_vlc.seekable() && m_vlc.lengthMs() > 0;
    // Height reserved under the video for the transport/quality controls.
    const float controlBarHeight = canSeek ? 86.0f : 46.0f;

    // Row of quality buttons sitting directly beneath the picture, one per
    // rendition the master playlist actually offers for this broadcast.
    auto drawControlBar = [&]() {
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kPanel));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 7));
        // Fullscreen insets the bar so it floats clear of the screen edges.
        const float barWidth = isFullscreen ? videoWidth - 24.0f : videoWidth;
        ImGui::BeginChild("##controlBar", ImVec2(barWidth, controlBarHeight),
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // --- row 1: scrub bar (VODs and recovered playlists) ----------------
        if (canSeek) {
            const int64_t lengthMs = m_vlc.lengthMs();
            const int64_t timeMs   = std::max<int64_t>(0, m_vlc.timeMs());

            ImGui::PushFont(m_fonts.small_);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(util::formatDuration((int)(timeMs / 1000)).c_str());
            ImGui::PopFont();
            ImGui::SameLine();

            const std::string total = util::formatDuration((int)(lengthMs / 1000));
            const float totalWidth  = ImGui::CalcTextSize(total.c_str()).x + 10.0f;

            // Dragging must not fight the position reported by the player, so
            // the slider owns the value while it is being held.
            static bool  scrubbing = false;
            static float scrubValue = 0.0f;
            float fraction = scrubbing ? scrubValue
                                       : (float)((double)timeMs / (double)lengthMs);

            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - totalWidth);
            if (ImGui::SliderFloat("##seek", &fraction, 0.0f, 1.0f, "")) {
                scrubbing  = true;
                scrubValue = fraction;
            }
            if (scrubbing && !ImGui::IsItemActive()) {
                m_vlc.setPosition(scrubValue);
                scrubbing = false;
            }

            ImGui::SameLine();
            ImGui::PushFont(m_fonts.small_);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
            ImGui::TextUnformatted(total.c_str());
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }

        // --- row 2: transport + quality -------------------------------------
        // Play/pause first, so the transport controls read left to right.
        if (playerPlaying() || m_vlc.state() == player::VlcPlayer::State::Paused) {
            bool paused = (m_vlc.state() == player::VlcPlayer::State::Paused);
            if (ui::ghostButton(paused ? "|>##pp" : "||##pp", 44, 30)) {
                if (s.playerBackend == 0) m_vlc.togglePause();
                else                      m_player.togglePause();
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", paused ? T("watch.play") : T("watch.pause"));
            ImGui::SameLine();
        }

        // Skip controls, useful for stepping past ad breaks in a recovered VOD.
        if (canSeek) {
            if (ui::ghostButton("<< 5s##back5", 62, 30)) m_vlc.seekRelative(-5.0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("watch.back5"));
            ImGui::SameLine();
            if (ui::ghostButton("5s >>##fwd5", 62, 30)) m_vlc.seekRelative(5.0);
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", T("watch.forward5"));
            ImGui::SameLine();
        }

        // "chunked" is the source rendition; give it a resolution-based name.
        auto prettyName = [](const twitch::Variant& v) {
            if (v.name != "chunked") return v.name;
            std::string label = v.height ? (std::to_string(v.height) + "p") : "Source";
            if (v.frameRate >= 50.0) label += std::to_string((int)std::lround(v.frameRate));
            return label;
        };

        if (!m_playlist.ok || m_playlist.variants.empty()) {
            ImGui::PushFont(m_fonts.small_);
            ImGui::AlignTextToFramePadding();
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
            ImGui::TextUnformatted(m_watchLoading ? T("common.loading") : T("watch.noQualities"));
            ImGui::PopStyleColor();
            ImGui::PopFont();
        } else {
            // A single button; the renditions open above it so the list never
            // falls off the bottom of the window.
            std::string current = T("common.quality");
            for (const auto& v : m_playlist.variants)
                if (v.name == m_watchSubtitle) current = prettyName(v);

            const std::string label = current + "  ^##qualityPick";
            const float width = ImGui::CalcTextSize(current.c_str()).x + 46.0f;
            if (ui::primaryButton(label.c_str(), width, 30)) ImGui::OpenPopup("##qualityMenu");

            // Pivot (0,1) anchors the popup's bottom edge to the button's top,
            // so it grows upwards.
            const ImVec2 buttonTopLeft = ImGui::GetItemRectMin();
            ImGui::SetNextWindowPos(ImVec2(buttonTopLeft.x, buttonTopLeft.y - 6.0f),
                                    ImGuiCond_Always, ImVec2(0.0f, 1.0f));

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 6));
            if (ImGui::BeginPopup("##qualityMenu")) {
                // Register the menu so the video window is clipped around it;
                // the player's native child window would otherwise cover it.
                const ImVec2 pos  = ImGui::GetWindowPos();
                const ImVec2 size = ImGui::GetWindowSize();
                registerVideoOverlay(pos.x, pos.y, size.x, size.y);

                for (size_t i = 0; i < m_playlist.variants.size(); ++i) {
                    const twitch::Variant& variant = m_playlist.variants[i];
                    const std::string name = prettyName(variant);
                    const bool selected = (variant.name == m_watchSubtitle);

                    std::string row = name + "##qm" + std::to_string(i);
                    if (ImGui::Selectable(row.c_str(), selected, 0, ImVec2(150, 24)))
                        applyQuality(variant.name);

                    if (variant.bandwidth > 0) {
                        ImGui::SameLine(96.0f);
                        ImGui::PushFont(m_fonts.small_);
                        ImGui::PushStyleColor(ImGuiCol_Text,
                                              ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
                        ImGui::Text("%.1f Mb", variant.bandwidth / 1000000.0);
                        ImGui::PopStyleColor();
                        ImGui::PopFont();
                    }
                }
                ImGui::EndPopup();
            }
            ImGui::PopStyleVar();
        }

        // --- volume + fullscreen, right-aligned in the same bar -------------
        const float volumeWidth = 160.0f;
        const float fsWidth     = 44.0f;
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - volumeWidth - fsWidth - 4.0f);
        ImGui::SetNextItemWidth(volumeWidth);
        if (ImGui::SliderInt("##volume", &m_playerVolume, 0, 100, "vol %d%%")) {
            playerSetVolume(m_playerVolume);
            s.volume = m_playerVolume;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) Config::get().save();

        ImGui::SameLine(0, 4);
        if (ui::fullscreenButton("fsToggle", fsWidth, 30, isFullscreen))
            toggleFullscreen();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s  (F11)", isFullscreen ? T("watch.exitFullscreen")
                                                        : T("watch.fullscreen"));

        // Only in fullscreen does the bar sit on top of the picture. Windowed
        // it lives below the video, and registering it there would merge with
        // the quality menu into one tall rect punched through the video.
        if (isFullscreen) {
            registerVideoOverlay(ImGui::GetWindowPos().x, ImGui::GetWindowPos().y,
                                 ImGui::GetWindowSize().x, ImGui::GetWindowSize().y);
        }

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();
    };

    // In fullscreen the controls float over the picture and fade out while the
    // pointer is still; moving the mouse brings them back.
    const double idleSeconds = ImGui::GetTime() - m_lastPointerMove;
    const bool   barVisible  = !isFullscreen || idleSeconds < 3.0;

    auto drawVideoPane = [&]() {
        ImGui::BeginGroup();
        const ImVec2 paneOrigin = ImGui::GetCursorPos();

        // Fullscreen: the video takes the whole area and the bar is layered on
        // top. Windowed: the bar sits underneath and takes its own strip.
        const float videoHeight = isFullscreen
                                ? paneHeight
                                : paneHeight - controlBarHeight -
                                      ImGui::GetStyle().ItemSpacing.y;

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.05f, 0.06f, 1.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, isFullscreen ? 0.0f : 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##videoPane", ImVec2(videoWidth, videoHeight),
                          ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);

        // mpv paints into a native child window layered over this rectangle, so
        // its bounds are synced to the pane every frame.
        ImVec2 origin = ImGui::GetWindowPos();
        ImVec2 size   = ImGui::GetWindowSize();
        const ImVec2 viewportPos = ImGui::GetMainViewport()->Pos;

        if (playerPlaying()) {
            playerSetVisible(true);
            playerSetBounds((int)(origin.x - viewportPos.x), (int)(origin.y - viewportPos.y),
                            (int)size.x, (int)size.y);

            // Cut the overlay's rectangle out of the video window so the menu
            // drawn over it is not hidden behind the native child window.
            if (m_videoOverlay[2] > 0.0f && m_videoOverlay[3] > 0.0f) {
                const float localX = m_videoOverlay[0] - origin.x;
                const float localY = m_videoOverlay[1] - origin.y;
                playerSetClipExclusion((int)localX, (int)localY,
                                       (int)m_videoOverlay[2], (int)m_videoOverlay[3]);
            } else {
                playerSetClipExclusion(0, 0, 0, 0);
            }
        } else {
            playerSetVisible(false);

            // Placeholder shown while resolving or after a failure.
            ImGui::SetCursorPos(ImVec2(size.x * 0.5f - 110.0f, size.y * 0.5f - 30.0f));
            ImGui::BeginGroup();
            if (m_watchLoading) {
                ui::spinner(12.0f, 3.0f);
                ImGui::Dummy(ImVec2(0, 8));
                ui::hint(T("watch.starting"));
            } else if (m_watchReconnecting) {
                // Usually the tail of an ad break: the stitched stream ended
                // and a fresh playlist is being fetched.
                ui::spinner(12.0f, 3.0f);
                ImGui::Dummy(ImVec2(0, 8));
                ui::hint(T("watch.reconnecting"));
            } else if (m_watchAdLoop) {
                // Reconnecting cannot help here: every new token replays an ad.
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + 380.0f);
                ui::hint(T("watch.adLoop"));
                ImGui::PopTextWrapPos();
                ImGui::Dummy(ImVec2(0, 12));

                if (ui::primaryButton(T("common.retry"), 180, 36)) {
                    m_watchAdLoop   = false;
                    m_shortSessions = 0;
                    m_watchRetries  = 0;
                    m_nextRetryAt   = 0.0;
                    reconnectLive();
                }
            } else if (m_watchGaveUp) {
                ui::hint(T("watch.reconnectFailed"));
                ImGui::Dummy(ImVec2(0, 10));
                if (ui::primaryButton(T("common.retry"), 180, 34)) {
                    m_watchRetries = 0;
                    m_watchGaveUp  = false;
                    m_nextRetryAt  = 0.0;
                    reconnectLive();
                }
            } else if (!playerAvailable()) {
                ui::hint(playerMissingHint().c_str());
                ImGui::Dummy(ImVec2(0, 10));
                if (ui::primaryButton(T("nav.settings"), 180, 34)) setScreen(Screen::Settings);
            } else {
                ui::hint(T("watch.nothing"));
            }
            ImGui::EndGroup();
        }

        // The seek bar lives in the control bar below, not here: this rectangle
        // is covered by the player's native child window, so anything drawn
        // over it would be invisible while video is playing.

        ImGui::EndChild();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor();

        if (isFullscreen) {
            if (barVisible) {
                // Rewind the cursor over the video so the bar overlays it.
                ImGui::SetCursorPos(ImVec2(paneOrigin.x + 12.0f,
                                           paneOrigin.y + videoHeight -
                                               controlBarHeight - 16.0f));
                drawControlBar();
            }
        } else {
            drawControlBar();
        }
        ImGui::EndGroup();
    };

    if (chatFirst) {
        drawChatPane();
        ImGui::SameLine();
        drawVideoPane();
    } else {
        drawVideoPane();
        if (chatVisible) {
            ImGui::SameLine();
            drawChatPane();
        }
    }
}

} // namespace dfts
