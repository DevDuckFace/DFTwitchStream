#pragma once
#include "core/WebSocket.h"

#include <string>
#include <vector>
#include <deque>
#include <mutex>
#include <atomic>
#include <cstdint>

namespace dfts::chat {

enum class MessageKind { Chat, System, Notice, Action, SubEvent, Error };

// A stretch of the message that is an emote rather than text. Offsets are byte
// positions into Message::text, already converted from the code-point indices
// Twitch sends.
struct EmoteRun {
    size_t      begin = 0;
    size_t      end   = 0;   // one past the last byte
    std::string id;

    // Twitch serves every emote from one CDN path; scale is "1.0", "2.0", "3.0".
    std::string url(const char* scale = "1.0") const {
        return "https://static-cdn.jtvnw.net/emoticons/v2/" + id +
               "/default/dark/" + scale;
    }
};

// A badge the sender wears, e.g. set "subscriber" version "12". The image URL
// has to be resolved through the Helix badge lists.
struct BadgeRef {
    std::string setId;
    std::string version;
};

struct Message {
    MessageKind kind = MessageKind::Chat;
    std::string user;         // display name
    std::string login;
    std::string text;
    std::string channel;
    uint32_t    color = 0;    // 0xRRGGBB, 0 = pick one from the login hash
    bool        moderator  = false;
    bool        subscriber = false;
    bool        vip        = false;
    int64_t     time = 0;

    std::vector<EmoteRun> emotes;   // sorted by begin, non-overlapping
    std::vector<BadgeRef> badges;   // in the order Twitch listed them
};

// Twitch IRC over WebSocket. Anonymous connections (justinfan) work without a
// token; passing one enables sending messages as the signed-in user.
class ChatClient {
public:
    ChatClient();
    ~ChatClient();

    ChatClient(const ChatClient&)            = delete;
    ChatClient& operator=(const ChatClient&) = delete;

    // Connects and joins `channel`. Reconnects if already running.
    void join(const std::string& channel, const std::string& nick = "",
              const std::string& oauthToken = "");
    void leave();

    bool connected() const { return m_socket.connected(); }
    bool canSend()   const { return m_authenticated.load() && connected(); }

    const std::string& channel() const { return m_channel; }

    // Queues a PRIVMSG. Ignored when not authenticated.
    void send(const std::string& text);

    // Copies out the buffered messages for rendering. This is a full copy, so
    // callers should only do it when version() changed rather than per frame.
    std::vector<Message> messages() const;
    size_t messageCount() const;
    void clear();

    // Increments whenever the buffer changes, letting the UI cache its copy.
    uint64_t version() const { return m_version.load(); }

    // Set once and reused for every reconnect.
    void setMaxMessages(size_t n) { m_maxMessages = n; }

private:
    void handleRaw(const std::string& payload);
    void handleLine(const std::string& line);
    void push(Message msg);

    net::WebSocket           m_socket;
    mutable std::mutex       m_mutex;
    std::deque<Message>      m_buffer;
    std::string              m_channel;
    std::string              m_nick;
    std::string              m_token;
    std::atomic<bool>        m_authenticated{false};
    std::atomic<uint64_t>    m_version{0};
    // Busy channels push hundreds of lines a minute; a smaller window keeps
    // wrapped-text layout cheap without losing useful scrollback.
    size_t                   m_maxMessages = 250;
    std::string              m_partial;    // incomplete line across frames
};

// Deterministic readable colour for a login that has no colour tag set.
uint32_t colorForLogin(const std::string& login);

} // namespace dfts::chat
