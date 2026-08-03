#include "twitch/TwitchApi.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace dfts::twitch {

namespace {

constexpr const char* kHelix = "https://api.twitch.tv/helix";

struct Page {
    bool        ok = false;
    json        data = json::array();
    std::string cursor;
    std::string error;
    int         status = 0;
};

Page fetchPage(const std::string& clientId, const std::string& token, const std::string& url) {
    Page p;
    http::Response r = http::get(url, {
        {"Client-Id",     clientId},
        {"Authorization", "Bearer " + token},
    });
    p.status = r.status;
    if (r.status == 0) { p.error = r.error; return p; }
    if (!r.ok()) {
        std::string msg = r.body;
        try {
            json j = json::parse(r.body);
            msg = j.value("message", msg);
        } catch (...) {}
        p.error = msg;
        return p;
    }
    try {
        json j = json::parse(r.body);
        if (j.contains("data") && j["data"].is_array()) p.data = j["data"];
        if (j.contains("pagination") && j["pagination"].is_object())
            p.cursor = j["pagination"].value("cursor", "");
        p.ok = true;
    } catch (const std::exception& e) {
        p.error = std::string("bad json: ") + e.what();
    }
    return p;
}

// Fetches every page of an endpoint, appending &after=<cursor> as it goes.
// `maxItems` of 0 means "no limit".
Page fetchAll(const std::string& clientId, const std::string& token,
              const std::string& baseUrl, size_t maxItems = 0) {
    Page acc;
    acc.data = json::array();
    std::string cursor;
    for (int page = 0; page < 50; ++page) {
        std::string url = baseUrl;
        if (!cursor.empty())
            url += (url.find('?') == std::string::npos ? "?" : "&") + std::string("after=") +
                   util::urlEncode(cursor);
        Page p = fetchPage(clientId, token, url);
        if (!p.ok) { p.data = acc.data; return p; }
        for (auto& item : p.data) acc.data.push_back(item);
        acc.ok     = true;
        acc.status = p.status;
        cursor     = p.cursor;
        if (cursor.empty()) break;
        if (maxItems && acc.data.size() >= maxItems) break;
    }
    return acc;
}

User parseUser(const json& j) {
    User u;
    u.id              = j.value("id", "");
    u.login           = j.value("login", "");
    u.displayName     = j.value("display_name", u.login);
    u.profileImageUrl = j.value("profile_image_url", "");
    u.description     = j.value("description", "");
    return u;
}

Stream parseStream(const json& j) {
    Stream s;
    s.userId        = j.value("user_id", "");
    s.userLogin     = j.value("user_login", "");
    s.userName      = j.value("user_name", s.userLogin);
    s.gameId        = j.value("game_id", "");
    s.gameName      = j.value("game_name", "");
    s.title         = j.value("title", "");
    s.type          = j.value("type", "live");
    s.thumbnailUrl  = j.value("thumbnail_url", "");
    s.startedAt     = j.value("started_at", "");
    s.language      = j.value("language", "");
    s.viewerCount   = j.value("viewer_count", (int64_t)0);
    s.startedAtUnix = util::parseIso8601(s.startedAt);
    return s;
}

// search/channels returns a slightly different shape than streams.
Stream parseSearchChannel(const json& j) {
    Stream s;
    s.userId       = j.value("id", "");
    s.userLogin    = j.value("broadcaster_login", "");
    s.userName     = j.value("display_name", s.userLogin);
    s.gameId       = j.value("game_id", "");
    s.gameName     = j.value("game_name", "");
    s.title        = j.value("title", "");
    s.thumbnailUrl = j.value("thumbnail_url", "");
    s.startedAt    = j.value("started_at", "");
    s.type         = j.value("is_live", false) ? "live" : "";
    s.language     = j.value("broadcaster_language", "");
    s.startedAtUnix = util::parseIso8601(s.startedAt);
    return s;
}

Game parseGame(const json& j) {
    Game g;
    g.id        = j.value("id", "");
    g.name      = j.value("name", "");
    g.boxArtUrl = j.value("box_art_url", "");
    return g;
}

Clip parseClip(const json& j) {
    Clip c;
    c.id               = j.value("id", "");
    c.url              = j.value("url", "");
    c.embedUrl         = j.value("embed_url", "");
    c.videoId          = j.value("video_id", "");
    c.gameId           = j.value("game_id", "");
    c.language         = j.value("language", "");
    c.broadcasterId    = j.value("broadcaster_id", "");
    c.broadcasterLogin = j.value("broadcaster_login", "");
    c.broadcasterName  = j.value("broadcaster_name", c.broadcasterLogin);
    c.creatorName      = j.value("creator_name", "");
    c.title            = j.value("title", "");
    c.thumbnailUrl     = j.value("thumbnail_url", "");
    c.createdAt        = j.value("created_at", "");
    c.viewCount        = j.value("view_count", (int64_t)0);
    c.duration         = j.value("duration", 0.0);
    c.featured         = j.value("is_featured", false);
    c.createdAtUnix    = util::parseIso8601(c.createdAt);
    // vod_offset is null once the source broadcast expires.
    if (j.contains("vod_offset") && j["vod_offset"].is_number())
        c.vodOffset = j["vod_offset"].get<int>();
    return c;
}

BadgeSet parseBadgeSet(const json& j) {
    BadgeSet set;
    set.setId = j.value("set_id", "");
    if (j.contains("versions") && j["versions"].is_array()) {
        for (const auto& v : j["versions"]) {
            BadgeVersion version;
            version.id    = v.value("id", "");
            version.title = v.value("title", "");
            version.url1x = v.value("image_url_1x", "");
            version.url2x = v.value("image_url_2x", "");
            version.url4x = v.value("image_url_4x", "");
            set.versions.push_back(std::move(version));
        }
    }
    return set;
}

Vod parseVod(const json& j) {
    Vod v;
    v.id            = j.value("id", "");
    v.streamId      = j.value("stream_id", "");
    v.userId        = j.value("user_id", "");
    v.userLogin     = j.value("user_login", "");
    v.userName      = j.value("user_name", v.userLogin);
    v.title         = j.value("title", "");
    v.description   = j.value("description", "");
    v.createdAt     = j.value("created_at", "");
    v.publishedAt   = j.value("published_at", "");
    v.url           = j.value("url", "");
    v.thumbnailUrl  = j.value("thumbnail_url", "");
    v.viewable      = j.value("viewable", "public");
    v.type          = j.value("type", "archive");
    v.duration      = j.value("duration", "");
    v.viewCount     = j.value("view_count", (int64_t)0);
    v.createdAtUnix = util::parseIso8601(v.createdAt);
    v.durationSecs  = util::parseTwitchDuration(v.duration);
    return v;
}

// Helix caps most id/login filters at 100 values per request.
std::string joinParams(const std::string& key, const std::vector<std::string>& values,
                       size_t begin, size_t end) {
    std::string out;
    for (size_t i = begin; i < end; ++i)
        out += (out.empty() ? "" : "&") + key + "=" + util::urlEncode(values[i]);
    return out;
}

} // namespace

std::string sizedThumb(const std::string& tmpl, int w, int h) {
    std::string s = util::replaceAll(tmpl, "{width}", std::to_string(w));
    s = util::replaceAll(s, "{height}", std::to_string(h));
    s = util::replaceAll(s, "%{width}", std::to_string(w));
    s = util::replaceAll(s, "%{height}", std::to_string(h));
    return s;
}

Result<User> Api::getCurrentUser() {
    Result<User> res;
    Page p = fetchPage(m_clientId, m_token, std::string(kHelix) + "/users");
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    if (p.data.empty()) { res.error = "no user in response"; return res; }
    res.value = parseUser(p.data[0]);
    res.ok    = true;
    return res;
}

Result<std::vector<User>> Api::getUsersByLogin(const std::vector<std::string>& logins) {
    Result<std::vector<User>> res;
    res.ok = true;
    for (size_t i = 0; i < logins.size(); i += 100) {
        size_t end = std::min(logins.size(), i + 100);
        std::string url = std::string(kHelix) + "/users?" + joinParams("login", logins, i, end);
        Page p = fetchPage(m_clientId, m_token, url);
        res.status = p.status;
        if (!p.ok) { res.ok = false; res.error = p.error; return res; }
        for (auto& item : p.data) res.value.push_back(parseUser(item));
    }
    return res;
}

Result<std::vector<User>> Api::getUsersById(const std::vector<std::string>& ids) {
    Result<std::vector<User>> res;
    res.ok = true;
    for (size_t i = 0; i < ids.size(); i += 100) {
        size_t end = std::min(ids.size(), i + 100);
        std::string url = std::string(kHelix) + "/users?" + joinParams("id", ids, i, end);
        Page p = fetchPage(m_clientId, m_token, url);
        res.status = p.status;
        if (!p.ok) { res.ok = false; res.error = p.error; return res; }
        for (auto& item : p.data) res.value.push_back(parseUser(item));
    }
    return res;
}

Result<std::vector<FollowedChannel>> Api::getFollowedChannels(const std::string& userId) {
    Result<std::vector<FollowedChannel>> res;
    std::string url = std::string(kHelix) + "/channels/followed?first=100&user_id=" +
                      util::urlEncode(userId);
    Page p = fetchAll(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) {
        FollowedChannel f;
        f.userId      = item.value("broadcaster_id", "");
        f.login       = item.value("broadcaster_login", "");
        f.displayName = item.value("broadcaster_name", f.login);
        f.followedAt  = item.value("followed_at", "");
        res.value.push_back(std::move(f));
    }
    res.ok = true;
    return res;
}

Result<std::vector<Stream>> Api::getFollowedStreams(const std::string& userId) {
    Result<std::vector<Stream>> res;
    std::string url = std::string(kHelix) + "/streams/followed?first=100&user_id=" +
                      util::urlEncode(userId);
    Page p = fetchAll(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseStream(item));
    res.ok = true;
    return res;
}

Result<std::vector<Stream>> Api::getStreamsByUserId(const std::vector<std::string>& userIds) {
    Result<std::vector<Stream>> res;
    res.ok = true;
    for (size_t i = 0; i < userIds.size(); i += 100) {
        size_t end = std::min(userIds.size(), i + 100);
        std::string url = std::string(kHelix) + "/streams?first=100&" +
                          joinParams("user_id", userIds, i, end);
        Page p = fetchAll(m_clientId, m_token, url);
        res.status = p.status;
        if (!p.ok) { res.ok = false; res.error = p.error; return res; }
        for (auto& item : p.data) res.value.push_back(parseStream(item));
    }
    return res;
}

Result<std::vector<Stream>> Api::getTopStreams(int limit, const std::string& language) {
    Result<std::vector<Stream>> res;
    std::string url = std::string(kHelix) + "/streams?first=" +
                      std::to_string(std::clamp(limit, 1, 100));
    if (!language.empty()) url += "&language=" + util::urlEncode(language);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseStream(item));
    res.ok = true;
    return res;
}

Result<std::vector<Stream>> Api::searchChannels(const std::string& query, int limit,
                                                bool liveOnly) {
    Result<std::vector<Stream>> res;
    if (util::trim(query).empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/search/channels?first=" +
                      std::to_string(std::clamp(limit, 1, 100)) +
                      "&query=" + util::urlEncode(query);
    if (liveOnly) url += "&live_only=true";
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseSearchChannel(item));
    res.ok = true;
    return res;
}

Result<std::vector<Game>> Api::getTopGames(int limit) {
    Result<std::vector<Game>> res;
    std::string url = std::string(kHelix) + "/games/top?first=" +
                      std::to_string(std::clamp(limit, 1, 100));
    Page p = fetchAll(m_clientId, m_token, url, (size_t)std::max(1, limit));
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) {
        res.value.push_back(parseGame(item));
        if ((int)res.value.size() >= limit) break;
    }
    res.ok = true;
    return res;
}

Result<std::vector<Game>> Api::searchCategories(const std::string& query, int limit) {
    Result<std::vector<Game>> res;
    if (util::trim(query).empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/search/categories?first=" +
                      std::to_string(std::clamp(limit, 1, 100)) +
                      "&query=" + util::urlEncode(query);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseGame(item));
    res.ok = true;
    return res;
}

Result<std::vector<Game>> Api::getGamesByName(const std::vector<std::string>& names) {
    Result<std::vector<Game>> res;
    res.ok = true;
    for (size_t i = 0; i < names.size(); i += 100) {
        size_t end = std::min(names.size(), i + 100);
        std::string url = std::string(kHelix) + "/games?" + joinParams("name", names, i, end);
        Page p = fetchPage(m_clientId, m_token, url);
        res.status = p.status;
        if (!p.ok) { res.ok = false; res.error = p.error; return res; }
        for (auto& item : p.data) res.value.push_back(parseGame(item));
    }
    return res;
}

Result<std::vector<Stream>> Api::getStreamsByGameId(const std::string& gameId, int limit,
                                                    const std::string& language) {
    Result<std::vector<Stream>> res;
    if (gameId.empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/streams?first=100&game_id=" +
                      util::urlEncode(gameId);
    if (!language.empty()) url += "&language=" + util::urlEncode(language);
    Page p = fetchAll(m_clientId, m_token, url, (size_t)std::max(1, limit));
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) {
        res.value.push_back(parseStream(item));
        if ((int)res.value.size() >= limit) break;
    }
    res.ok = true;
    return res;
}

std::string Clip::guessMp4Url() const {
    // The preview image and the media file differ only in their suffix, e.g.
    //   .../AT-cm|<id>-preview-480x272.jpg  ->  .../AT-cm|<id>.mp4
    // Unofficial but stable, and the only route when the download endpoint
    // is not available to this application.
    size_t marker = thumbnailUrl.find("-preview-");
    if (marker == std::string::npos) return {};
    return thumbnailUrl.substr(0, marker) + ".mp4";
}

Result<std::vector<Clip>> Api::getClips(const std::string& broadcasterId, int limit, int days) {
    Result<std::vector<Clip>> res;
    if (broadcasterId.empty()) { res.ok = true; return res; }

    std::string url = std::string(kHelix) + "/clips?first=100&broadcaster_id=" +
                      util::urlEncode(broadcasterId);
    if (days > 0) {
        // The window is inclusive of both ends; Twitch orders by views inside it.
        url += "&started_at=" + util::urlEncode(util::formatIso8601(
                   util::unixNow() - (int64_t)days * 86400));
        url += "&ended_at="   + util::urlEncode(util::formatIso8601(util::unixNow()));
    }

    Page p = fetchAll(m_clientId, m_token, url, (size_t)std::max(1, limit));
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) {
        res.value.push_back(parseClip(item));
        if ((int)res.value.size() >= limit) break;
    }
    res.ok = true;
    return res;
}

Result<std::string> Api::getClipDownloadUrl(const std::string& clipId) {
    Result<std::string> res;
    if (clipId.empty()) { res.ok = true; return res; }

    std::string url = std::string(kHelix) + "/clips/downloads?clip_id=" +
                      util::urlEncode(clipId);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }

    // The field name is not pinned down in the reference, so accept the
    // plausible spellings rather than failing on an unexpected key.
    if (!p.data.empty()) {
        const json& first = p.data[0];
        for (const char* key : {"download_url", "url", "downloadUrl"}) {
            if (first.contains(key) && first[key].is_string()) {
                res.value = first[key].get<std::string>();
                break;
            }
        }
    }
    res.ok = true;
    return res;
}

Result<std::vector<BadgeSet>> Api::getGlobalBadges() {
    Result<std::vector<BadgeSet>> res;
    Page p = fetchPage(m_clientId, m_token, std::string(kHelix) + "/chat/badges/global");
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseBadgeSet(item));
    res.ok = true;
    return res;
}

Result<std::vector<BadgeSet>> Api::getChannelBadges(const std::string& broadcasterId) {
    Result<std::vector<BadgeSet>> res;
    if (broadcasterId.empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/chat/badges?broadcaster_id=" +
                      util::urlEncode(broadcasterId);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) res.value.push_back(parseBadgeSet(item));
    res.ok = true;
    return res;
}

Result<ChatSettings> Api::getChatSettings(const std::string& broadcasterId) {
    Result<ChatSettings> res;
    if (broadcasterId.empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/chat/settings?broadcaster_id=" +
                      util::urlEncode(broadcasterId);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    if (p.data.empty()) { res.ok = true; return res; }

    const json& j = p.data[0];
    ChatSettings s;
    s.slowMode       = j.value("slow_mode", false);
    s.slowModeWait   = j.value("slow_mode_wait_time", 0);
    s.followerMode   = j.value("follower_mode", false);
    s.followerMinutes= j.value("follower_mode_duration", 0);
    s.subscriberMode = j.value("subscriber_mode", false);
    s.emoteMode      = j.value("emote_mode", false);
    s.uniqueMode     = j.value("unique_chat_mode", false);
    res.value = s;
    res.ok    = true;
    return res;
}

Result<ChannelInfo> Api::getChannelInfo(const std::string& broadcasterId) {
    Result<ChannelInfo> res;
    if (broadcasterId.empty()) { res.ok = true; return res; }
    std::string url = std::string(kHelix) + "/channels?broadcaster_id=" +
                      util::urlEncode(broadcasterId);
    Page p = fetchPage(m_clientId, m_token, url);
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    if (p.data.empty()) { res.error = "channel not found"; return res; }

    const json& j = p.data[0];
    ChannelInfo info;
    info.broadcasterId  = j.value("broadcaster_id", "");
    info.login          = j.value("broadcaster_login", "");
    info.name           = j.value("broadcaster_name", info.login);
    info.language       = j.value("broadcaster_language", "");
    info.gameId         = j.value("game_id", "");
    info.gameName       = j.value("game_name", "");
    info.title          = j.value("title", "");
    info.delaySeconds   = j.value("delay", 0);
    info.brandedContent = j.value("is_branded_content", false);
    if (j.contains("tags") && j["tags"].is_array())
        info.tags = j["tags"].get<std::vector<std::string>>();
    if (j.contains("content_classification_labels") &&
        j["content_classification_labels"].is_array())
        info.contentLabels = j["content_classification_labels"].get<std::vector<std::string>>();

    res.value = info;
    res.ok    = true;
    return res;
}

Result<std::vector<Vod>> Api::getVideosByUser(const std::string& userId,
                                              const std::string& type, int limit) {
    Result<std::vector<Vod>> res;
    std::string url = std::string(kHelix) + "/videos?first=100&sort=time&user_id=" +
                      util::urlEncode(userId);
    if (type != "all") url += "&type=" + util::urlEncode(type);
    Page p = fetchAll(m_clientId, m_token, url, (size_t)std::max(1, limit));
    res.status = p.status;
    if (!p.ok) { res.error = p.error; return res; }
    for (auto& item : p.data) {
        res.value.push_back(parseVod(item));
        if ((int)res.value.size() >= limit) break;
    }
    res.ok = true;
    return res;
}

Result<std::vector<Vod>> Api::getVideosById(const std::vector<std::string>& ids) {
    Result<std::vector<Vod>> res;
    res.ok = true;
    for (size_t i = 0; i < ids.size(); i += 100) {
        size_t end = std::min(ids.size(), i + 100);
        std::string url = std::string(kHelix) + "/videos?" + joinParams("id", ids, i, end);
        Page p = fetchPage(m_clientId, m_token, url);
        res.status = p.status;
        if (!p.ok) { res.ok = false; res.error = p.error; return res; }
        for (auto& item : p.data) res.value.push_back(parseVod(item));
    }
    return res;
}

} // namespace dfts::twitch
