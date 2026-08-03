#include "core/Config.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>
#include <dpapi.h>
#include <wincrypt.h>

#include <fstream>
#include <sstream>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace dfts {

namespace {
// Public client id used by the desktop app when the user has not registered
// their own Twitch application. Device Code Flow needs no client secret.
constexpr const char* kDefaultClientId = "q6batx0epp608isickayubi39itsckt";
} // namespace

std::string protectString(const std::string& plain) {
    if (plain.empty()) return {};
    DATA_BLOB in{(DWORD)plain.size(), (BYTE*)plain.data()};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"DFTwitchStream", nullptr, nullptr, nullptr, 0, &out)) {
        LOG_W("CryptProtectData failed; token will not be stored");
        return {};
    }
    DWORD b64len = 0;
    CryptBinaryToStringA(out.pbData, out.cbData,
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &b64len);
    std::string b64(b64len, '\0');
    CryptBinaryToStringA(out.pbData, out.cbData,
                         CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, b64.data(), &b64len);
    LocalFree(out.pbData);
    b64.resize(strlen(b64.c_str()));
    return b64;
}

std::string unprotectString(const std::string& base64Blob) {
    if (base64Blob.empty()) return {};
    DWORD binLen = 0;
    if (!CryptStringToBinaryA(base64Blob.c_str(), (DWORD)base64Blob.size(),
                              CRYPT_STRING_BASE64, nullptr, &binLen, nullptr, nullptr))
        return {};
    std::vector<BYTE> bin(binLen);
    if (!CryptStringToBinaryA(base64Blob.c_str(), (DWORD)base64Blob.size(),
                              CRYPT_STRING_BASE64, bin.data(), &binLen, nullptr, nullptr))
        return {};

    DATA_BLOB in{binLen, bin.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out)) return {};
    std::string plain((char*)out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

Config& Config::get() {
    static Config instance;
    return instance;
}

Config::Config() {
    m_path = util::appDataDir() + "\\config.json";
}

std::string Config::effectiveClientId() const {
    return m_settings.clientId.empty() ? kDefaultClientId : m_settings.clientId;
}

std::string Config::effectiveDownloadDir() const {
    std::string dir = m_settings.downloadDir.empty() ? util::defaultDownloadDir()
                                                     : m_settings.downloadDir;
    util::ensureDir(dir);
    return dir;
}

bool Config::load() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::ifstream f(util::widen(m_path));
    if (!f) return false;

    json j;
    try {
        f >> j;
    } catch (const std::exception& e) {
        LOG_E(std::string("config parse error: ") + e.what());
        return false;
    }

    auto& s = m_settings;
    s.language        = j.value("language", "en") == "pt" ? Language::Portuguese : Language::English;
    s.clientId        = j.value("clientId", "");
    s.userLogin       = j.value("userLogin", "");
    s.userId          = j.value("userId", "");
    s.userDisplayName = j.value("userDisplayName", "");
    s.tokenExpiry     = j.value("tokenExpiry", (int64_t)0);
    s.accessToken     = unprotectString(j.value("accessTokenEnc", ""));
    s.refreshToken    = unprotectString(j.value("refreshTokenEnc", ""));

    s.playerBackend    = j.value("playerBackend", 0);
    s.mpvPath          = j.value("mpvPath", "");
    s.ffmpegPath       = j.value("ffmpegPath", "");
    s.preferredQuality = j.value("preferredQuality", "best");
    s.lowLatency       = j.value("lowLatency", true);
    s.volume           = j.value("volume", 80);

    s.chatEnabled    = j.value("chatEnabled", true);
    s.chatWidth      = j.value("chatWidth", 360);
    s.chatOnRight    = j.value("chatOnRight", true);
    s.chatTimestamps = j.value("chatTimestamps", true);
    s.thirdPartyEmotes = j.value("thirdPartyEmotes", true);
    s.chatFontScale  = j.value("chatFontScale", 100);

    s.downloadDir     = j.value("downloadDir", "");
    s.downloadThreads = j.value("downloadThreads", 8);
    s.muxWithFfmpeg   = j.value("muxWithFfmpeg", true);
    s.keepSegments    = j.value("keepSegments", false);

    s.browseLanguage      = j.value("browseLanguage", "");
    s.showOfflineChannels = j.value("showOfflineChannels", false);
    s.gridColumns         = j.value("gridColumns", 4);
    s.uiScale             = j.value("uiScale", 1.0f);
    s.startMaximized      = j.value("startMaximized", true);

    if (j.contains("recentRecoveryUrls") && j["recentRecoveryUrls"].is_array())
        s.recentRecoveryUrls = j["recentRecoveryUrls"].get<std::vector<std::string>>();

    return true;
}

bool Config::save() {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto& s = m_settings;
    json j;
    j["language"]        = (s.language == Language::Portuguese) ? "pt" : "en";
    j["clientId"]        = s.clientId;
    j["userLogin"]       = s.userLogin;
    j["userId"]          = s.userId;
    j["userDisplayName"] = s.userDisplayName;
    j["tokenExpiry"]     = s.tokenExpiry;
    j["accessTokenEnc"]  = protectString(s.accessToken);
    j["refreshTokenEnc"] = protectString(s.refreshToken);

    j["playerBackend"]    = s.playerBackend;
    j["mpvPath"]          = s.mpvPath;
    j["ffmpegPath"]       = s.ffmpegPath;
    j["preferredQuality"] = s.preferredQuality;
    j["lowLatency"]       = s.lowLatency;
    j["volume"]           = s.volume;

    j["chatEnabled"]    = s.chatEnabled;
    j["chatWidth"]      = s.chatWidth;
    j["chatOnRight"]    = s.chatOnRight;
    j["chatTimestamps"] = s.chatTimestamps;
    j["thirdPartyEmotes"] = s.thirdPartyEmotes;
    j["chatFontScale"]  = s.chatFontScale;

    j["downloadDir"]     = s.downloadDir;
    j["downloadThreads"] = s.downloadThreads;
    j["muxWithFfmpeg"]   = s.muxWithFfmpeg;
    j["keepSegments"]    = s.keepSegments;

    j["browseLanguage"]      = s.browseLanguage;
    j["showOfflineChannels"] = s.showOfflineChannels;
    j["gridColumns"]         = s.gridColumns;
    j["uiScale"]             = s.uiScale;
    j["startMaximized"]      = s.startMaximized;
    j["recentRecoveryUrls"]  = s.recentRecoveryUrls;

    std::ofstream f(util::widen(m_path), std::ios::trunc);
    if (!f) { LOG_E("cannot write " + m_path); return false; }
    f << j.dump(2);
    return true;
}

void Config::clearSession() {
    auto& s = m_settings;
    s.accessToken.clear();
    s.refreshToken.clear();
    s.userLogin.clear();
    s.userId.clear();
    s.userDisplayName.clear();
    s.tokenExpiry = 0;
    save();
}

} // namespace dfts
