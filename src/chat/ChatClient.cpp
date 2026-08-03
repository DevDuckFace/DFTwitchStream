#include "chat/ChatClient.h"
#include "core/Util.h"
#include "core/Log.h"

#include <algorithm>
#include <map>

namespace dfts::chat {

namespace {

constexpr const char* kChatWs = "wss://irc-ws.chat.twitch.tv:443";

// Splits "@key=value;key2=value2" into a map. IRCv3 escapes \s as space.
std::map<std::string, std::string> parseTags(const std::string& tagBlob) {
    std::map<std::string, std::string> tags;
    for (const auto& pair : util::split(tagBlob, ';')) {
        size_t eq = pair.find('=');
        if (eq == std::string::npos) { tags[pair] = ""; continue; }
        std::string key = pair.substr(0, eq);
        std::string val = pair.substr(eq + 1);
        val = util::replaceAll(val, "\\s", " ");
        val = util::replaceAll(val, "\\:", ";");
        val = util::replaceAll(val, "\\\\", "\\");
        tags[key] = val;
    }
    return tags;
}

uint32_t parseHexColor(const std::string& s) {
    if (s.size() != 7 || s[0] != '#') return 0;
    return (uint32_t)std::strtoul(s.c_str() + 1, nullptr, 16);
}

// Twitch counts emote positions in code points, but the message we store is
// UTF-8. This builds the code-point -> byte offset table for one message.
std::vector<size_t> codePointOffsets(const std::string& text) {
    std::vector<size_t> offsets;
    offsets.reserve(text.size() + 1);
    for (size_t i = 0; i < text.size();) {
        offsets.push_back(i);
        const unsigned char c = (unsigned char)text[i];
        // Surrogate-pair emoji count as two units on Twitch's side, matching
        // UTF-16; a 4-byte sequence therefore occupies two code-point slots.
        if      (c < 0x80) i += 1;
        else if ((c >> 5) == 0x06) i += 2;
        else if ((c >> 4) == 0x0E) i += 3;
        else if ((c >> 3) == 0x1E) { offsets.push_back(i); i += 4; }
        else i += 1;
    }
    offsets.push_back(text.size());
    return offsets;
}

// Parses "25:0-4,12-16/1902:6-10" into byte-offset runs over `text`.
std::vector<EmoteRun> parseEmoteTag(const std::string& tag, const std::string& text) {
    std::vector<EmoteRun> runs;
    if (tag.empty()) return runs;

    const std::vector<size_t> offsets = codePointOffsets(text);
    auto byteAt = [&](long index) -> size_t {
        if (index < 0) return 0;
        if ((size_t)index >= offsets.size()) return text.size();
        return offsets[(size_t)index];
    };

    for (const auto& group : util::split(tag, '/')) {
        const size_t colon = group.find(':');
        if (colon == std::string::npos) continue;
        const std::string id = group.substr(0, colon);

        for (const auto& range : util::split(group.substr(colon + 1), ',')) {
            const size_t dash = range.find('-');
            if (dash == std::string::npos) continue;
            const long from = std::strtol(range.substr(0, dash).c_str(), nullptr, 10);
            const long to   = std::strtol(range.substr(dash + 1).c_str(), nullptr, 10);
            if (to < from) continue;

            EmoteRun run;
            run.id    = id;
            run.begin = byteAt(from);
            run.end   = byteAt(to + 1);   // Twitch's end index is inclusive
            if (run.end > run.begin && run.end <= text.size()) runs.push_back(std::move(run));
        }
    }

    std::sort(runs.begin(), runs.end(),
              [](const EmoteRun& a, const EmoteRun& b) { return a.begin < b.begin; });
    return runs;
}

// Parses "moderator/1,subscriber/12" into set/version pairs.
std::vector<BadgeRef> parseBadgeTag(const std::string& tag) {
    std::vector<BadgeRef> badges;
    for (const auto& entry : util::split(tag, ',')) {
        const size_t slash = entry.find('/');
        if (slash == std::string::npos) continue;
        BadgeRef ref;
        ref.setId   = util::trim(entry.substr(0, slash));
        ref.version = util::trim(entry.substr(slash + 1));
        if (!ref.setId.empty()) badges.push_back(std::move(ref));
    }
    return badges;
}

// Twitch wraps /me messages in ACTION control characters.
bool stripAction(std::string& text) {
    const std::string prefix = "\x01" "ACTION ";
    if (!util::startsWith(text, prefix) || text.empty() || text.back() != '\x01') return false;
    text = text.substr(prefix.size(), text.size() - prefix.size() - 1);
    return true;
}

} // namespace

uint32_t colorForLogin(const std::string& login) {
    // Twitch's own default palette, chosen so names stay legible on dark grey.
    static const uint32_t palette[] = {
        0xFF0000, 0x0000FF, 0x008000, 0xB22222, 0xFF7F50, 0x9ACD32, 0xFF4500,
        0x2E8B57, 0xDAA520, 0xD2691E, 0x5F9EA0, 0x1E90FF, 0xFF69B4, 0x8A2BE2,
        0x00FF7F,
    };
    uint32_t hash = 2166136261u;
    for (unsigned char c : login) { hash ^= c; hash *= 16777619u; }
    return palette[hash % (sizeof(palette) / sizeof(palette[0]))];
}

ChatClient::ChatClient() {
    m_socket.onMessage([this](const std::string& payload) { handleRaw(payload); });
    m_socket.onState([this](bool up, const std::string& info) {
        Message m;
        m.kind = up ? MessageKind::System : MessageKind::Error;
        m.time = util::unixNow();
        m.text = up ? ("connected to " + info) : ("disconnected: " + info);
        push(std::move(m));

        if (up) {
            // Tags give us colours and badges; commands give us JOIN/NOTICE.
            m_socket.sendText("CAP REQ :twitch.tv/tags twitch.tv/commands");
            if (!m_token.empty() && !m_nick.empty()) {
                m_socket.sendText("PASS oauth:" + m_token);
                m_socket.sendText("NICK " + util::lower(m_nick));
            } else {
                // Anonymous read-only login.
                m_socket.sendText("NICK justinfan" + std::to_string(10000 + (std::rand() % 80000)));
            }
            if (!m_channel.empty()) m_socket.sendText("JOIN #" + util::lower(m_channel));
        } else {
            m_authenticated = false;
        }
    });
}

ChatClient::~ChatClient() { leave(); }

void ChatClient::join(const std::string& channel, const std::string& nick,
                      const std::string& oauthToken) {
    leave();
    // Start every channel with an empty log: leftovers from the previous room
    // are confusing and only make the first layout pass more expensive.
    clear();
    m_channel = util::lower(util::trim(channel));
    m_nick    = nick;
    m_token   = oauthToken;
    if (m_channel.empty()) return;

    LOG_I("chat: joining #" + m_channel);
    m_socket.connect(kChatWs);
}

void ChatClient::leave() {
    if (m_socket.connected() && !m_channel.empty())
        m_socket.sendText("PART #" + m_channel);
    m_socket.close();
    m_authenticated = false;
    m_partial.clear();
}

void ChatClient::send(const std::string& text) {
    std::string body = util::trim(text);
    if (body.empty() || m_channel.empty()) return;
    if (!canSend()) return;

    m_socket.sendText("PRIVMSG #" + m_channel + " :" + body);

    // Twitch does not echo our own PRIVMSG back, so render it locally.
    Message m;
    m.kind    = MessageKind::Chat;
    m.user    = m_nick;
    m.login   = util::lower(m_nick);
    m.text    = body;
    m.channel = m_channel;
    m.color   = colorForLogin(m.login);
    m.time    = util::unixNow();
    push(std::move(m));
}

std::vector<Message> ChatClient::messages() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return {m_buffer.begin(), m_buffer.end()};
}

size_t ChatClient::messageCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_buffer.size();
}

void ChatClient::clear() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_buffer.clear();
    }
    ++m_version;
}

void ChatClient::push(Message msg) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_buffer.size() >= m_maxMessages) m_buffer.pop_front();
        m_buffer.push_back(std::move(msg));
    }
    ++m_version;
}

void ChatClient::handleRaw(const std::string& payload) {
    // A frame can carry several IRC lines and can split one across frames.
    m_partial += payload;
    size_t start = 0;
    for (;;) {
        size_t nl = m_partial.find('\n', start);
        if (nl == std::string::npos) break;
        std::string line = m_partial.substr(start, nl - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) handleLine(line);
        start = nl + 1;
    }
    m_partial.erase(0, start);
}

void ChatClient::handleLine(const std::string& line) {
    // Keep the connection alive; Twitch drops silent clients.
    if (util::startsWith(line, "PING")) {
        m_socket.sendText("PONG :tmi.twitch.tv");
        return;
    }

    std::string rest = line;
    std::map<std::string, std::string> tags;
    if (!rest.empty() && rest[0] == '@') {
        size_t sp = rest.find(' ');
        if (sp == std::string::npos) return;
        tags = parseTags(rest.substr(1, sp - 1));
        rest = rest.substr(sp + 1);
    }

    std::string prefix;
    if (!rest.empty() && rest[0] == ':') {
        size_t sp = rest.find(' ');
        if (sp == std::string::npos) return;
        prefix = rest.substr(1, sp - 1);
        rest   = rest.substr(sp + 1);
    }

    size_t sp = rest.find(' ');
    std::string command = (sp == std::string::npos) ? rest : rest.substr(0, sp);
    std::string params  = (sp == std::string::npos) ? ""   : rest.substr(sp + 1);

    auto trailing = [&]() -> std::string {
        size_t colon = params.find(" :");
        if (colon != std::string::npos) return params.substr(colon + 2);
        if (!params.empty() && params[0] == ':') return params.substr(1);
        return {};
    };
    auto senderLogin = [&]() -> std::string {
        size_t bang = prefix.find('!');
        return util::lower(bang == std::string::npos ? prefix : prefix.substr(0, bang));
    };

    if (command == "PRIVMSG") {
        Message m;
        m.time    = util::unixNow();
        m.login   = senderLogin();
        m.user    = tags.count("display-name") && !tags["display-name"].empty()
                    ? tags["display-name"] : m.login;
        m.text    = trailing();
        m.channel = m_channel;
        m.color   = tags.count("color") ? parseHexColor(tags["color"]) : 0;
        if (m.color == 0) m.color = colorForLogin(m.login);

        std::string badges = tags.count("badges") ? tags["badges"] : "";
        m.moderator  = tags["mod"] == "1" || util::icontains(badges, "moderator") ||
                       util::icontains(badges, "broadcaster");
        m.subscriber = tags["subscriber"] == "1" || util::icontains(badges, "subscriber");
        m.vip        = util::icontains(badges, "vip");
        m.badges     = parseBadgeTag(badges);

        // Strip the ACTION wrapper before mapping emote positions: Twitch
        // indexes them against the visible text, not the control sequence.
        if (stripAction(m.text)) m.kind = MessageKind::Action;
        if (tags.count("emotes"))
            m.emotes = parseEmoteTag(tags["emotes"], m.text);
        push(std::move(m));
        return;
    }

    if (command == "USERNOTICE") {
        Message m;
        m.kind = MessageKind::SubEvent;
        m.time = util::unixNow();
        m.user = tags.count("display-name") ? tags["display-name"] : senderLogin();
        m.text = tags.count("system-msg") ? tags["system-msg"] : trailing();
        m.color = 0x9147FF;
        if (!m.text.empty()) push(std::move(m));
        return;
    }

    if (command == "NOTICE") {
        Message m;
        m.kind  = MessageKind::Notice;
        m.time  = util::unixNow();
        m.text  = trailing();
        push(std::move(m));
        return;
    }

    if (command == "CLEARCHAT") {
        std::string target = trailing();
        Message m;
        m.kind = MessageKind::Notice;
        m.time = util::unixNow();
        m.text = target.empty() ? "chat cleared by a moderator" : (target + " was timed out");
        push(std::move(m));
        return;
    }

    if (command == "001") {          // RPL_WELCOME: the PASS/NICK pair was accepted
        m_authenticated = !m_token.empty();
        return;
    }

    if (command == "JOIN" && senderLogin() == util::lower(m_nick)) {
        Message m;
        m.kind = MessageKind::System;
        m.time = util::unixNow();
        m.text = "joined #" + m_channel;
        push(std::move(m));
        return;
    }

    if (command == "RECONNECT") {
        LOG_W("chat: server asked us to reconnect");
        std::string ch = m_channel, nick = m_nick, token = m_token;
        m_socket.close();
        join(ch, nick, token);
        return;
    }
}

} // namespace dfts::chat
