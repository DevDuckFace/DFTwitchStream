#include "app/App.h"
#include "ui/Theme.h"
#include "core/Util.h"
#include "i18n/I18n.h"

#include <imgui.h>

namespace dfts {

void App::drawLogin() {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float  cardWidth = 520.0f;

    // Centre the card in whatever space the screen was given.
    ImGui::SetCursorPos(ImVec2(std::max(0.0f, (avail.x - cardWidth) * 0.5f),
                               std::max(24.0f, avail.y * 0.12f)));

    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::ColorConvertU32ToFloat4(ui::col::kPanel));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 16.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(38, 34));
    ImGui::BeginChild("##loginCard", ImVec2(cardWidth, 0),
                      ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    // --- brand -------------------------------------------------------------
    ImGui::PushFont(m_fonts.large);
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kAccent));
    ImGui::TextUnformatted("DF Twitch Stream");
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 6));

    ImGui::PushFont(m_fonts.medium);
    ImGui::TextUnformatted(T("login.heading"));
    ImGui::PopFont();
    ui::hint(T("login.subtitle"));
    ImGui::Dummy(ImVec2(0, 18));

    const twitch::Auth::State state = m_auth.state();
    const twitch::Auth::DeviceCode device = m_auth.deviceCode();

    // --- idle: offer to start ---------------------------------------------
    if (!m_loginInProgress || state == twitch::Auth::State::Idle) {
        if (ui::primaryButton(T("login.start"), -1.0f, 44.0f)) startLogin();
        ImGui::Dummy(ImVec2(0, 10));
        ImGui::PushFont(m_fonts.small_);
        ui::hint(T("login.scopes"));
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 16));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0, 12));

        if (ui::ghostButton(T("login.skip"), -1.0f, 38.0f)) setScreen(Screen::Recovery);
        ImGui::PushFont(m_fonts.small_);
        ui::hint(T("login.anonNote"));
        ImGui::PopFont();

        if (!m_loginError.empty()) {
            ImGui::Dummy(ImVec2(0, 12));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kError));
            ImGui::TextWrapped("%s", m_loginError.c_str());
            ImGui::PopStyleColor();
        }
    }
    // --- requesting the code ----------------------------------------------
    else if (state == twitch::Auth::State::RequestingCode) {
        ui::centerNextItem(20.0f);
        ui::spinner(10.0f, 3.0f);
        ImGui::Dummy(ImVec2(0, 8));
        ui::hint(T("common.loading"));
        ImGui::Dummy(ImVec2(0, 12));
        if (ui::ghostButton(T("common.cancel"), -1.0f, 36.0f)) cancelLogin();
    }
    // --- waiting for the user to enter the code ---------------------------
    else if (state == twitch::Auth::State::AwaitingUser) {
        ImGui::TextUnformatted(T("login.instructions"));
        ImGui::Dummy(ImVec2(0, 12));

        // The code itself, large and centred so it is easy to read off-screen.
        ImGui::PushFont(m_fonts.huge);
        float codeWidth = ImGui::CalcTextSize(device.userCode.c_str()).x;
        ui::centerNextItem(codeWidth);
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kAccent));
        ImGui::TextUnformatted(device.userCode.c_str());
        ImGui::PopStyleColor();
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 16));
        float half = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
        if (ui::primaryButton(T("login.openPage"), half, 40.0f))
            util::openInBrowser(device.verificationUri);
        ImGui::SameLine();
        if (ui::ghostButton(T("login.copyCode"), half, 40.0f)) {
            util::setClipboard(device.userCode);
            toastOk(T("common.copied"));
        }

        ImGui::Dummy(ImVec2(0, 14));
        ImGui::PushFont(m_fonts.small_);
        ui::hint(device.verificationUri.c_str());
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 10));
        ImGui::BeginGroup();
        ui::spinner(7.0f, 2.5f);
        ImGui::SameLine(0, 10);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 1);
        ui::hint(T("login.waiting"));
        ImGui::EndGroup();

        int left = m_auth.secondsLeft();
        if (left > 0) {
            ImGui::PushFont(m_fonts.small_);
            ui::hint(I18n::get().fmt("login.expiresIn", util::formatDuration(left)).c_str());
            ImGui::PopFont();
        }

        ImGui::Dummy(ImVec2(0, 12));
        if (ui::ghostButton(T("common.cancel"), -1.0f, 36.0f)) cancelLogin();
    }
    // --- expired or failed -------------------------------------------------
    else {
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(ui::col::kError));
        ImGui::TextWrapped("%s", m_auth.error().empty() ? T("common.error") : m_auth.error().c_str());
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0, 14));
        if (ui::primaryButton(T("common.retry"), -1.0f, 40.0f)) startLogin();
    }

    // --- language switch, always available --------------------------------
    ImGui::Dummy(ImVec2(0, 20));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));

    Language current = Config::get().settings().language;
    float pairWidth = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

    auto langButton = [&](const char* label, Language lang) {
        bool active = (current == lang);
        bool clicked = active ? ui::primaryButton(label, pairWidth, 34.0f)
                              : ui::ghostButton(label, pairWidth, 34.0f);
        if (clicked && !active) applyLanguage(lang);
    };
    langButton("English##lang", Language::English);
    ImGui::SameLine();
    // Explicit UTF-8 bytes: u8"" is char8_t under C++20 and will not bind to const char*.
    langButton("Portugu\xC3\xAAs (BR)##lang", Language::Portuguese);

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

} // namespace dfts
