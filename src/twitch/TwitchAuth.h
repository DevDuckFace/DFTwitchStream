#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>

namespace dfts::twitch {

// OAuth Device Code Flow. The user types a short code on twitch.tv/activate,
// which avoids embedding a client secret or hosting a redirect URI.
class Auth {
public:
    enum class State { Idle, RequestingCode, AwaitingUser, Success, Failed, Expired };

    struct DeviceCode {
        std::string deviceCode;
        std::string userCode;
        std::string verificationUri;
        int         interval  = 5;   // seconds between polls
        int         expiresIn = 1800;
    };

    struct Session {
        std::string accessToken;
        std::string refreshToken;
        int64_t     expiresAt = 0;
    };

    ~Auth();

    // Begins the flow on a background thread. `onDone` fires once with the
    // final state; poll state()/deviceCode() from the UI thread meanwhile.
    void begin(const std::string& clientId,
               std::function<void(State, const Session&, const std::string& error)> onDone);
    void cancel();

    State       state() const { return m_state.load(); }
    DeviceCode  deviceCode() const;
    std::string error() const;
    int         secondsLeft() const;

    // Exchanges a refresh token for a fresh access token. Blocking.
    static bool refresh(const std::string& clientId, const std::string& refreshToken,
                        Session& out, std::string& error);

    // Revokes a token server-side. Blocking, best-effort.
    static void revoke(const std::string& clientId, const std::string& accessToken);

    // Validates a token and fills in login/user id. Blocking.
    static bool validate(const std::string& accessToken, std::string& login,
                         std::string& userId, int64_t& expiresAt, std::string& error);

    static std::string scopeString();

private:
    void run(std::string clientId,
             std::function<void(State, const Session&, const std::string&)> onDone);

    mutable std::mutex  m_mutex;
    std::thread         m_thread;
    std::atomic<bool>   m_running{false};
    std::atomic<State>  m_state{State::Idle};
    DeviceCode          m_device;
    std::string         m_error;
    int64_t             m_deadline = 0;
};

} // namespace dfts::twitch
