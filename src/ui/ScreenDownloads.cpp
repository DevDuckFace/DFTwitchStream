#include "app/App.h"
#include "ui/Theme.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <windows.h>
#include <shellapi.h>

namespace dfts {

namespace {

const char* stateLabel(vod::JobState state) {
    switch (state) {
        case vod::JobState::Queued:           return T("common.pending");
        case vod::JobState::FetchingPlaylist: return T("common.loading");
        case vod::JobState::Downloading:      return T("common.running");
        case vod::JobState::Muxing:           return T("downloads.muxing");
        case vod::JobState::Done:             return T("common.done");
        case vod::JobState::Failed:           return T("common.failed");
        case vod::JobState::Cancelled:        return T("common.cancelled");
    }
    return "";
}

ImU32 stateColor(vod::JobState state) {
    switch (state) {
        case vod::JobState::Done:      return ui::col::kOk;
        case vod::JobState::Failed:    return ui::col::kError;
        case vod::JobState::Cancelled: return ui::col::kWarn;
        default:                       return ui::col::kAccent;
    }
}

// Opens Explorer with the file selected, so the user lands on the result.
void revealInExplorer(const std::string& path) {
    if (path.empty()) return;
    std::wstring arg = L"/select,\"" + util::widen(path) + L"\"";
    ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
}

} // namespace

void App::drawDownloads() {
    auto& manager = vod::DownloadManager::get();
    std::vector<vod::JobStatus> jobs = manager.snapshot();

    // --- toolbar -----------------------------------------------------------
    ImGui::BeginGroup();
    if (ui::ghostButton(T("common.openFolder"), 180, 0))
        util::openInBrowser(Config::get().effectiveDownloadDir());
    ImGui::SameLine();
    if (ui::ghostButton(T("downloads.clearDone"), 180, 0)) manager.clearFinished();
    ImGui::SameLine();
    if (ui::dangerButton(T("downloads.cancel"), 140, 0)) manager.cancelAll();
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 6));
    ImGui::PushFont(m_fonts.small_);
    ui::hint(Config::get().effectiveDownloadDir().c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 10));

    if (jobs.empty()) {
        ui::hint(T("downloads.empty"));
        return;
    }

    ImGui::BeginChild("##downloadScroll", ImVec2(0, 0));

    for (const vod::JobStatus& job : jobs) {
        std::string id = "##job" + std::to_string(job.id);
        ui::beginCard(id.c_str(), ImVec2(-1, 116.0f));

        float inner = ImGui::GetContentRegionAvail().x;

        // --- title + state pill ---
        ImGui::PushFont(m_fonts.medium);
        ui::textEllipsis(job.title.c_str(), inner - 130.0f);
        ImGui::PopFont();

        ImGui::SameLine(inner - 118.0f);
        ImGui::PushFont(m_fonts.small_);
        ui::badge(stateLabel(job.state), stateColor(job.state));
        ImGui::PopFont();

        // --- progress ---
        ImGui::Dummy(ImVec2(0, 4));
        char overlay[160];
        if (job.segmentsTotal > 0) {
            std::snprintf(overlay, sizeof(overlay), "%d / %d  -  %s",
                          job.segmentsDone, job.segmentsTotal,
                          util::formatBytes(job.bytes).c_str());
        } else {
            std::snprintf(overlay, sizeof(overlay), "%s", util::formatBytes(job.bytes).c_str());
        }
        ui::progressBar(job.progress, ImVec2(inner - 250.0f, 20.0f), overlay);

        // --- actions ---
        ImGui::SameLine();
        if (!job.finished()) {
            if (ui::dangerButton((std::string(T("downloads.cancel")) + id).c_str(), 110, 22))
                manager.cancel(job.id);
        } else if (job.state == vod::JobState::Done) {
            if (ui::primaryButton((std::string(T("vods.play")) + id).c_str(), 90, 22))
                watchDirectUrl(job.outputPath, job.title, "");
            ImGui::SameLine();
            if (ui::ghostButton((std::string(T("common.openFolder")) + id).c_str(), 130, 22))
                revealInExplorer(job.outputPath);
        }

        // --- footer: speed + output path or error ---
        ImGui::PushFont(m_fonts.small_);
        if (job.bytesPerSec > 1.0 && !job.finished()) {
            std::string speed = std::string(T("downloads.speed")) + ": " +
                                util::formatBytes((uint64_t)job.bytesPerSec) + "/s";
            ui::textEllipsis(speed.c_str(), inner, ui::col::kTextDim);
        }
        if (!job.message.empty()) {
            ui::textEllipsis(job.message.c_str(), inner, ui::col::kWarn);
        } else if (!job.outputPath.empty()) {
            ui::textEllipsis(job.outputPath.c_str(), inner, ui::col::kTextDim);
        }
        ImGui::PopFont();

        ui::endCard();
        ImGui::Dummy(ImVec2(0, 4));
    }

    ImGui::EndChild();
}

} // namespace dfts
