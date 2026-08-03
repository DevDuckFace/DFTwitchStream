#include "app/App.h"
#include "ui/Theme.h"
#include "core/Util.h"
#include "core/Log.h"
#include "i18n/I18n.h"

#include <imgui.h>
#include <algorithm>

namespace dfts {

namespace {

// Shows "found: <path>" or "not found" under a tool path field.
void toolStatus(const std::string& resolved) {
    ImU32 color = resolved.empty() ? ui::col::kWarn : ui::col::kOk;
    std::string text = resolved.empty()
                     ? std::string(T("settings.notFound"))
                     : std::string(T("settings.found")) + ": " + resolved;
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

} // namespace

void App::drawSettings() {
    Settings& s = Config::get().settings();

    if (!ImGui::BeginTabBar("##settingsTabs")) return;

    // -----------------------------------------------------------------------
    // General
    // -----------------------------------------------------------------------
    if (ImGui::BeginTabItem(T("settings.appearance"))) {
        ImGui::Dummy(ImVec2(0, 10));

        ui::sectionHeader(T("settings.language"));
        int langIndex = (s.language == Language::Portuguese) ? 1 : 0;
        // Explicit UTF-8 bytes: u8"" is char8_t under C++20 and will not bind to const char*.
        const char* langs[] = {"English", "Portugu\xC3\xAAs (BR)"};
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::Combo("##lang", &langIndex, langs, IM_ARRAYSIZE(langs)))
            applyLanguage(langIndex == 1 ? Language::Portuguese : Language::English);

        ImGui::Dummy(ImVec2(0, 14));
        ui::sectionHeader(T("settings.appearance"));

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderFloat(T("settings.uiScale"), &s.uiScale, 0.8f, 1.6f, "%.2fx")) {
            m_settingsDirty = true;
        }
        if (ImGui::IsItemDeactivatedAfterEdit()) {
            rebuildFonts();
            Config::get().save();
        }

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderInt(T("settings.gridColumns"), &s.gridColumns, 1, 6))
            m_settingsDirty = true;

        if (ImGui::Checkbox(T("settings.showOffline"), &s.showOfflineChannels))
            m_settingsDirty = true;
        if (ImGui::Checkbox("Start maximized", &s.startMaximized))
            m_settingsDirty = true;

        ImGui::EndTabItem();
    }

    // -----------------------------------------------------------------------
    // Playback
    // -----------------------------------------------------------------------
    if (ImGui::BeginTabItem(T("settings.playback"))) {
        ImGui::Dummy(ImVec2(0, 10));
        ui::sectionHeader(T("settings.playback"));

        const char* backends[] = {T("settings.backendVlc"), T("settings.backendMpv")};
        ImGui::SetNextItemWidth(320.0f);
        if (ImGui::Combo(T("settings.backend"), &s.playerBackend, backends, IM_ARRAYSIZE(backends))) {
            // Switching engines mid-playback would leave two video surfaces up.
            playerStop();
            m_settingsDirty = true;
            Config::get().save();
        }
        ImGui::PushFont(m_fonts.small_);
        ui::hint(T("settings.backendHint"));
        if (s.playerBackend == 0) {
            std::string engine = "libVLC " + player::VlcPlayer::versionString();
            toolStatus(m_vlc.available() ? engine : std::string());
        }
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, 10));

        const char* qualities[] = {"best", "1080p60", "1080p30", "720p60", "720p30",
                                   "480p30", "360p30", "160p30", "audio_only", "worst"};
        int qualityIndex = 0;
        for (int i = 0; i < IM_ARRAYSIZE(qualities); ++i)
            if (s.preferredQuality == qualities[i]) qualityIndex = i;

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::Combo(T("settings.quality"), &qualityIndex, qualities, IM_ARRAYSIZE(qualities))) {
            s.preferredQuality = qualities[qualityIndex];
            m_settingsDirty = true;
        }

        if (ImGui::Checkbox(T("settings.lowLatency"), &s.lowLatency)) m_settingsDirty = true;

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderInt(T("settings.volume"), &s.volume, 0, 100)) {
            m_playerVolume = s.volume;
            playerSetVolume(s.volume);
            m_settingsDirty = true;
        }

        ImGui::Dummy(ImVec2(0, 14));
        ui::sectionHeader(T("settings.chat"));

        if (ImGui::Checkbox(T("settings.chatEnabled"), &s.chatEnabled))     m_settingsDirty = true;
        if (ImGui::Checkbox(T("settings.chatOnRight"), &s.chatOnRight))     m_settingsDirty = true;
        if (ImGui::Checkbox(T("settings.chatTimestamps"), &s.chatTimestamps)) m_settingsDirty = true;
        if (ImGui::Checkbox(T("settings.thirdPartyEmotes"), &s.thirdPartyEmotes)) {
            m_settingsDirty = true;
            Config::get().save();
            // Reload the sets for the room currently open.
            if (!m_watchChannel.empty()) loadChannelExtras(m_watchChannel);
        }
        ImGui::PushFont(m_fonts.small_);
        ui::hint(T("settings.thirdPartyHint"));
        ImGui::PopFont();

        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderInt(T("settings.chatWidth"), &s.chatWidth, 240, 600, "%d px"))
            m_settingsDirty = true;
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderInt(T("settings.chatFontScale"), &s.chatFontScale, 70, 160, "%d%%"))
            m_settingsDirty = true;

        ImGui::EndTabItem();
    }

    // -----------------------------------------------------------------------
    // Downloads
    // -----------------------------------------------------------------------
    if (ImGui::BeginTabItem(T("settings.downloads"))) {
        ImGui::Dummy(ImVec2(0, 10));
        ui::sectionHeader(T("settings.downloads"));

        ImGui::PushFont(m_fonts.small_);
        ImGui::TextUnformatted(T("settings.downloadDir"));
        ImGui::PopFont();
        if (ui::inputText("##downloadDir", m_setDownloadDir, sizeof(m_setDownloadDir),
                          util::defaultDownloadDir().c_str(), 560.0f)) {
            s.downloadDir = m_setDownloadDir;
            m_settingsDirty = true;
        }
        ImGui::SameLine();
        if (ui::ghostButton(T("common.openFolder"), 150, 0))
            util::openInBrowser(Config::get().effectiveDownloadDir());

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::SetNextItemWidth(280.0f);
        if (ImGui::SliderInt(T("settings.downloadThreads"), &s.downloadThreads, 1, 24))
            m_settingsDirty = true;

        if (ImGui::Checkbox(T("settings.mux"), &s.muxWithFfmpeg))       m_settingsDirty = true;
        if (ImGui::Checkbox(T("settings.keepSegments"), &s.keepSegments)) m_settingsDirty = true;

        ImGui::EndTabItem();
    }

    // -----------------------------------------------------------------------
    // External tools
    // -----------------------------------------------------------------------
    if (ImGui::BeginTabItem(T("settings.tools"))) {
        ImGui::Dummy(ImVec2(0, 10));
        ui::sectionHeader(T("settings.tools"));

        ImGui::PushFont(m_fonts.small_);
        ImGui::TextUnformatted(T("settings.mpvPath"));
        ImGui::PopFont();
        if (ui::inputText("##mpvPath", m_setMpvPath, sizeof(m_setMpvPath),
                          T("common.auto"), 640.0f)) {
            s.mpvPath = m_setMpvPath;
            m_settingsDirty = true;
        }
        toolStatus(player::MpvHost::detectMpv(s.mpvPath));

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushFont(m_fonts.small_);
        ImGui::TextUnformatted(T("settings.ffmpegPath"));
        ImGui::PopFont();
        if (ui::inputText("##ffmpegPath", m_setFfmpegPath, sizeof(m_setFfmpegPath),
                          T("common.auto"), 640.0f)) {
            s.ffmpegPath = m_setFfmpegPath;
            m_settingsDirty = true;
        }
        toolStatus(s.ffmpegPath.empty() ? util::findOnPath("ffmpeg") : s.ffmpegPath);

        ImGui::Dummy(ImVec2(0, 16));
        ui::sectionHeader(T("settings.account"));

        ImGui::PushFont(m_fonts.small_);
        ImGui::TextUnformatted(T("settings.clientId"));
        ImGui::PopFont();
        if (ui::inputText("##clientId", m_setClientId, sizeof(m_setClientId),
                          T("settings.clientIdHint"), 400.0f)) {
            s.clientId = m_setClientId;
            m_settingsDirty = true;
        }
        ImGui::PushFont(m_fonts.small_);
        ui::hint(T("settings.clientIdHint"));
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 12));
        if (s.loggedIn()) {
            std::string who = I18n::get().fmt("login.success", s.userDisplayName);
            ImGui::TextUnformatted(who.c_str());
            ImGui::Dummy(ImVec2(0, 8));
            if (ui::dangerButton(T("login.logout"), 200, 36)) m_showLogoutConfirm = true;
        } else {
            if (ui::primaryButton(T("login.start"), 200, 36)) setScreen(Screen::Login);
        }

        ImGui::Dummy(ImVec2(0, 12));
        if (ui::ghostButton(T("settings.openConfig"), 220, 34))
            util::openInBrowser(util::appDataDir());

        ImGui::EndTabItem();
    }

    ImGui::EndTabBar();

    // --- save bar ----------------------------------------------------------
    ImGui::Dummy(ImVec2(0, 16));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    if (ui::primaryButton(T("common.save"), 200, 40)) {
        s.mpvPath        = m_setMpvPath;
        s.ffmpegPath     = m_setFfmpegPath;
        s.downloadDir    = m_setDownloadDir;
        s.clientId       = m_setClientId;
        Config::get().save();
        m_settingsDirty = false;
        toastOk(T("common.saved"));
    }
    if (m_settingsDirty) {
        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kWarn));
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("*");
        ImGui::PopStyleColor();
    }
}

// ---------------------------------------------------------------------------
// Logs
// ---------------------------------------------------------------------------
void App::drawLogs() {
    ImGui::BeginGroup();
    if (ui::ghostButton(T("common.clear"), 140, 0)) Log::get().clear();
    ImGui::SameLine();
    if (ui::ghostButton(T("common.openFolder"), 160, 0)) util::openInBrowser(util::appDataDir());
    ImGui::SameLine();
    if (ui::ghostButton(T("common.copy"), 120, 0)) {
        std::string all;
        for (const auto& e : Log::get().snapshot())
            all += util::formatIso8601(e.time) + "  " + e.text + "\n";
        util::setClipboard(all);
        toastOk(T("common.copied"));
    }
    ImGui::EndGroup();

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::PushFont(m_fonts.small_);
    ui::hint(Log::get().filePath().c_str());
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::BeginChild("##logScroll", ImVec2(0, 0), ImGuiChildFlags_Borders);
    ImGui::PushFont(m_fonts.small_);

    std::vector<LogEntry> entries = Log::get().snapshot();
    ImGuiListClipper clipper;
    clipper.Begin((int)entries.size());
    while (clipper.Step()) {
        for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
            const LogEntry& e = entries[i];
            ImU32 color = ui::col::kTextDim;
            switch (e.level) {
                case LogLevel::Warn:  color = ui::col::kWarn;  break;
                case LogLevel::Error: color = ui::col::kError; break;
                case LogLevel::Info:  color = ui::col::kText;  break;
                default: break;
            }
            std::string stamp = util::formatLocalDateTime(e.time);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kTextDim));
            ImGui::TextUnformatted(stamp.c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine(0, 10);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(color));
            ImGui::TextWrapped("%s", e.text.c_str());
            ImGui::PopStyleColor();
        }
    }
    clipper.End();

    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 4.0f) ImGui::SetScrollHereY(1.0f);
    ImGui::PopFont();
    ImGui::EndChild();
}

// ---------------------------------------------------------------------------
// About
// ---------------------------------------------------------------------------
void App::drawAbout() {
    ImGui::Dummy(ImVec2(0, 10));

    ImGui::PushFont(m_fonts.large);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kAccent));
    ImGui::TextUnformatted("DF Twitch Stream " DFTS_VERSION);
    ImGui::PopStyleColor();
    ImGui::PopFont();

    ImGui::Dummy(ImVec2(0, 10));
    ui::hint("A native Windows client that combines a Twitch stream browser with player and "
             "chat, plus recovery and download of broadcasts that are no longer listed.");

    ImGui::Dummy(ImVec2(0, 18));
    ui::sectionHeader("Features");
    ImGui::BulletText("Twitch sign-in via OAuth Device Code Flow");
    ImGui::BulletText("Followed channels, live status and category");
    ImGui::BulletText("Saved VOD browsing, playback and download");
    ImGui::BulletText("VOD recovery for broadcasts removed from the site");
    ImGui::BulletText("Embedded mpv player with live chat beside the video");
    ImGui::BulletText("Parallel segment downloader with ffmpeg remuxing");

    ImGui::Dummy(ImVec2(0, 18));
    ui::sectionHeader("Requirements");
    ImGui::BulletText("mpv - video playback");
    ImGui::BulletText("ffmpeg - converting downloads to MP4 (optional)");

    ImGui::Dummy(ImVec2(0, 16));
    if (ui::ghostButton("mpv.io", 160, 34))       util::openInBrowser("https://mpv.io/installation/");
    ImGui::SameLine();
    if (ui::ghostButton("ffmpeg.org", 160, 34))   util::openInBrowser("https://ffmpeg.org/download.html");

    ImGui::Dummy(ImVec2(0, 18));
    ImGui::PushFont(m_fonts.small_);
    ui::hint("Not affiliated with or endorsed by Twitch Interactive, Inc. "
             "Use it only for content you are allowed to access and keep.");
    ImGui::PopFont();
}

} // namespace dfts
