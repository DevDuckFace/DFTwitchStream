#include "app/App.h"
#include "ui/Theme.h"
#include "core/Util.h"
#include "core/Http.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <algorithm>

namespace dfts {

void App::drawRecovery() {
    ImGui::PushFont(m_fonts.small_);
    ui::hint(T("recovery.explain"));
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 12));

    // --- URL parser --------------------------------------------------------
    ui::sectionHeader(T("recovery.urlLabel"));

    float parseButtonWidth = 150.0f;
    float inputWidth = ImGui::GetContentRegionAvail().x - parseButtonWidth -
                       ImGui::GetStyle().ItemSpacing.x;
    bool submitted = ui::inputText("##recUrl", m_recUrl, sizeof(m_recUrl),
                                   "https://twitchtracker.com/<canal>/streams/<id>",
                                   inputWidth, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    if (ui::ghostButton(T("common.search"), parseButtonWidth, 0) || submitted) {
        vod::RecoveryQuery parsed;
        std::string note;
        if (vod::parseTrackerUrl(m_recUrl, parsed, note)) {
            std::snprintf(m_recStreamer, sizeof(m_recStreamer), "%s", parsed.streamer.c_str());
            std::snprintf(m_recStreamId, sizeof(m_recStreamId), "%s", parsed.streamId.c_str());
            if (parsed.startTime > 0) {
                std::string iso = util::formatIso8601(parsed.startTime);
                iso = util::replaceAll(iso, "T", " ");
                iso = util::replaceAll(iso, "Z", "");
                std::snprintf(m_recStartTime, sizeof(m_recStartTime), "%s", iso.c_str());
            }
            m_recMessage = I18n::get().fmt("recovery.parsedFrom", note);
        } else {
            m_recMessage = T("recovery.badInput");
        }
    }

    ImGui::Dummy(ImVec2(0, 14));

    // --- manual fields -----------------------------------------------------
    ui::sectionHeader(T("recovery.manual"));

    float third = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x * 2) / 3.0f;

    ImGui::BeginGroup();
    ImGui::PushFont(m_fonts.small_);
    ImGui::TextUnformatted(T("recovery.streamer"));
    ImGui::PopFont();
    ui::inputText("##recStreamer", m_recStreamer, sizeof(m_recStreamer), "xqc", third);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(m_fonts.small_);
    ImGui::TextUnformatted(T("recovery.streamId"));
    ImGui::PopFont();
    ui::inputText("##recStreamId", m_recStreamId, sizeof(m_recStreamId), "39377349325", third);
    ImGui::EndGroup();

    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::PushFont(m_fonts.small_);
    ImGui::TextUnformatted(T("recovery.startTime"));
    ImGui::PopFont();
    ui::inputText("##recStart", m_recStartTime, sizeof(m_recStartTime),
                  "2024-01-31 20:15:03", third);
    ImGui::EndGroup();

    ImGui::PushFont(m_fonts.small_);
    ui::hint(T("recovery.timeHint"));
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 10));

    // --- scan controls -----------------------------------------------------
    ImGui::BeginGroup();
    if (m_recScanning) {
        if (ui::dangerButton(T("common.cancel"), 200, 40)) m_recCancel = true;
    } else {
        if (ui::primaryButton(T("recovery.scan"), 200, 40)) startRecoveryScan();
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(240.0f);
    // Published start times are usually rounded, so a window of neighbouring
    // seconds is searched around the given timestamp.
    ImGui::SliderInt("+/- s##recWindow", &m_recWindow, 0, 180);
    ImGui::EndGroup();

    if (m_recScanning) {
        ImGui::Dummy(ImVec2(0, 10));
        float fraction = m_recProgress.total > 0
                       ? (float)m_recProgress.checked / (float)m_recProgress.total
                       : 0.0f;
        std::string label = I18n::get().fmt("recovery.scanning",
                                            std::to_string(m_recProgress.checked),
                                            std::to_string(m_recProgress.total));
        ui::progressBar(fraction, ImVec2(-1, 22), label.c_str());
    }

    if (!m_recMessage.empty()) {
        ImGui::Dummy(ImVec2(0, 8));
        ImU32 color = m_recResults.empty() ? ui::col::kWarn : ui::col::kOk;
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
        ImGui::TextWrapped("%s", m_recMessage.c_str());
        ImGui::PopStyleColor();
    }

    // --- results -----------------------------------------------------------
    if (!m_recResults.empty()) {
        ImGui::Dummy(ImVec2(0, 16));
        ui::sectionHeader(T("recovery.results"));

        if (m_recMuted.mutedCount > 0) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kWarn));
            ImGui::TextWrapped("%s", I18n::get().fmt("recovery.muted",
                                                     std::to_string(m_recMuted.mutedCount)).c_str());
            ImGui::PopStyleColor();
            ImGui::PushFont(m_fonts.small_);
            ui::hint(T("recovery.unmute"));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 6));
        }

        ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("##recTable", 3, flags)) {
            ImGui::TableSetupColumn(T("common.quality"), ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("URL",               ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("",                  ImGuiTableColumnFlags_WidthFixed, 330.0f);
            ImGui::TableHeadersRow();

            for (size_t i = 0; i < m_recResults.size(); ++i) {
                const vod::RecoveredPlaylist& item = m_recResults[i];
                ImGui::TableNextRow();
                ImGui::PushID((int)i);

                ImGui::TableSetColumnIndex(0);
                ImGui::AlignTextToFramePadding();
                if (item.quality == "chunked") {
                    ImGui::PushFont(m_fonts.small_);
                    ui::badge("source", ui::col::kAccent);
                    ImGui::PopFont();
                } else {
                    ImGui::TextUnformatted(item.quality.c_str());
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::AlignTextToFramePadding();
                ui::textEllipsis(item.url.c_str(), ImGui::GetContentRegionAvail().x,
                                 ui::col::kTextDim);

                ImGui::TableSetColumnIndex(2);
                if (ui::primaryButton(T("recovery.downloadAll"), 150, 0)) {
                    std::string title = std::string(m_recStreamer) + "_" + m_recStreamId +
                                        "_" + item.quality;
                    downloadPlaylist(item.url, title);
                }
                ImGui::SameLine();
                if (ui::ghostButton(T("recovery.openPlaylist"), 90, 0))
                    watchDirectUrl(item.url,
                                   std::string(m_recStreamer) + " " + item.quality, "");
                ImGui::SameLine();
                if (ui::ghostButton(T("common.copy"), 70, 0)) {
                    util::setClipboard(item.url);
                    toastOk(T("common.copied"));
                }

                ImGui::PopID();
            }
            ImGui::EndTable();
        }
    }

    // --- recent searches ---------------------------------------------------
    const auto& recent = Config::get().settings().recentRecoveryUrls;
    if (!recent.empty()) {
        ImGui::Dummy(ImVec2(0, 18));
        ui::sectionHeader(T("recovery.recent"));
        for (size_t i = 0; i < recent.size(); ++i) {
            ImGui::PushID((int)(1000 + i));
            if (ui::ghostButton((recent[i] + "##recent").c_str(), -1, 30.0f)) {
                // Stored as "streamer / streamId / time".
                std::vector<std::string> parts = util::split(recent[i], '/');
                if (parts.size() >= 3) {
                    std::snprintf(m_recStreamer, sizeof(m_recStreamer), "%s",
                                  util::trim(parts[0]).c_str());
                    std::snprintf(m_recStreamId, sizeof(m_recStreamId), "%s",
                                  util::trim(parts[1]).c_str());
                    std::string rest = util::trim(recent[i].substr(
                        recent[i].find(parts[1]) + parts[1].size()));
                    if (!rest.empty() && rest[0] == '/') rest = util::trim(rest.substr(1));
                    std::snprintf(m_recStartTime, sizeof(m_recStartTime), "%s", rest.c_str());
                }
            }
            ImGui::PopID();
        }
        if (ui::ghostButton(T("common.clear"), 140, 30)) {
            Config::get().settings().recentRecoveryUrls.clear();
            Config::get().save();
        }
    }
}

} // namespace dfts
