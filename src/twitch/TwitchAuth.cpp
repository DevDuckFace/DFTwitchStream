#include "twitch/TwitchAuth.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <chrono>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace dfts::twitch {

namespace {
constexpr const char* kDeviceUrl   = "https://id.twitch.tv/oauth2/device";
constexpr const char* kTokenUrl    = "https://id.twitch.tv/oauth2/token";
constexpr const char* kRevokeUrl   = "https://id.twitch.tv/oauth2/revoke";
constexpr const char* kValidateUrl = "https://id.twitch.tv/oauth2/validate";

json parseOrEmpty(const std::string& body) {
    try { return json::parse(body); } catch (...) { return json::object(); }
}
} // namespace

std::string Auth::scopeString() {
    // Only what the app actually uses: followed channels + chat read/write.
    return "user:read:follows user:read:chat user:write:chat chat:read chat:edit";
}

Auth::~Auth() { cancel(); }

Auth::DeviceCode Auth::deviceCode() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_device;
}

std::string Auth::error() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

int Auth::secondsLeft() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_deadline == 0) return 0;
    return (int)std::max<int64_t>(0, m_deadline - util::unixNow());
}

void Auth::cancel() {
    m_running = false;
    if (m_thread.joinable()) m_thread.join();
}

void Auth::begin(const std::string& clientId,
                 std::function<void(State, const Session&, const std::string&)> onDone) {
    cancel();
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_device = {};
        m_error.clear();
        m_deadline = 0;
    }
    m_state   = State::RequestingCode;
    m_running = true;
    m_thread  = std::thread(&Auth::run, this, clientId, std::move(onDone));
}

void Auth::run(std::string clientId,
               std::function<void(State, const Session&, const std::string&)> onDone) {
    Session session;
    auto fail = [&](State st, const std::string& msg) {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_error = msg;
        }
        m_state = st;
        LOG_E("auth: " + msg);
        if (onDone) onDone(st, session, msg);
    };

    // --- step 1: request a device code ------------------------------------
    std::string form = "client_id=" + util::urlEncode(clientId) +
                       "&scopes="   + util::urlEncode(scopeString());
    http::Response resp = http::post(kDeviceUrl, form, {},
                                     "application/x-www-form-urlencoded");
    if (resp.status == 0) { fail(State::Failed, resp.error); return; }
    if (!resp.ok())       { fail(State::Failed, "HTTP " + std::to_string(resp.status) + ": " + resp.body); return; }

    json j = parseOrEmpty(resp.body);
    DeviceCode dc;
    dc.deviceCode      = j.value("device_code", "");
    dc.userCode        = j.value("user_code", "");
    dc.verificationUri = j.value("verification_uri", "https://www.twitch.tv/activate");
    dc.interval        = j.value("interval", 5);
    dc.expiresIn       = j.value("expires_in", 1800);
    if (dc.deviceCode.empty() || dc.userCode.empty()) {
        fail(State::Failed, "malformed device code response");
        return;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_device   = dc;
        m_deadline = util::unixNow() + dc.expiresIn;
    }
    m_state = State::AwaitingUser;
    LOG_I("auth: device code issued, waiting for user");

    // --- step 2: poll for the token ---------------------------------------
    const int64_t deadline = util::unixNow() + dc.expiresIn;
    int interval = std::max(1, dc.interval);

    while (m_running.load()) {
        for (int i = 0; i < interval * 10 && m_running.load(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (!m_running.load()) { m_state = State::Idle; return; }

        if (util::unixNow() > deadline) {
            fail(State::Expired, "device code expired");
            return;
        }

        std::string body = "client_id="   + util::urlEncode(clientId) +
                           "&device_code=" + util::urlEncode(dc.deviceCode) +
                           "&grant_type=urn:ietf:params:oauth:grant-type:device_code" +
                           "&scopes="     + util::urlEncode(scopeString());
        http::Response tr = http::post(kTokenUrl, body, {},
                                       "application/x-www-form-urlencoded");
        if (tr.status == 0) continue;   // transient network trouble; keep polling

        json tj = parseOrEmpty(tr.body);
        if (tr.ok() && tj.contains("access_token")) {
            session.accessToken  = tj.value("access_token", "");
            session.refreshToken = tj.value("refresh_token", "");
            session.expiresAt    = util::unixNow() + tj.value("expires_in", 0);
            m_state = State::Success;
            LOG_I("auth: token granted");
            if (onDone) onDone(State::Success, session, "");
            return;
        }

        std::string msg = tj.value("message", tj.value("error", ""));
        std::string low = util::lower(msg);
        if (util::icontains(low, "authorization_pending") || util::icontains(low, "pending"))
            continue;
        if (util::icontains(low, "slow_down")) { interval += 2; continue; }
        if (util::icontains(low, "expired")) { fail(State::Expired, msg); return; }
        if (msg.empty()) continue;
        fail(State::Failed, msg);
        return;
    }
    m_state = State::Idle;
}

bool Auth::refresh(const std::string& clientId, const std::string& refreshToken,
                   Session& out, std::string& error) {
    std::string body = "client_id="     + util::urlEncode(clientId) +
                       "&grant_type=refresh_token&refresh_token=" + util::urlEncode(refreshToken);
    http::Response r = http::post(kTokenUrl, body, {}, "application/x-www-form-urlencoded");
    if (!r.ok()) {
        error = r.status ? ("HTTP " + std::to_string(r.status) + ": " + r.body) : r.error;
        return false;
    }
    json j = parseOrEmpty(r.body);
    out.accessToken  = j.value("access_token", "");
    out.refreshToken = j.value("refresh_token", refreshToken);
    out.expiresAt    = util::unixNow() + j.value("expires_in", 0);
    return !out.accessToken.empty();
}

void Auth::revoke(const std::string& clientId, const std::string& accessToken) {
    if (accessToken.empty()) return;
    std::string body = "client_id=" + util::urlEncode(clientId) +
                       "&token="    + util::urlEncode(accessToken);
    http::post(kRevokeUrl, body, {}, "application/x-www-form-urlencoded");
}

bool Auth::validate(const std::string& accessToken, std::string& login, std::string& userId,
                    int64_t& expiresAt, std::string& error) {
    http::Response r = http::get(kValidateUrl, {{"Authorization", "OAuth " + accessToken}});
    if (!r.ok()) {
        error = r.status ? ("HTTP " + std::to_string(r.status)) : r.error;
        return false;
    }
    json j    = parseOrEmpty(r.body);
    login     = j.value("login", "");
    userId    = j.value("user_id", "");
    expiresAt = util::unixNow() + j.value("expires_in", 0);
    return !userId.empty();
}

} // namespace dfts::twitch
