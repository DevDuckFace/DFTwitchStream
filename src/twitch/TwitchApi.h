#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace dfts::twitch {

struct User {
    std::string id, login, displayName, profileImageUrl, description;
};

struct Stream {
    std::string userId, userLogin, userName;
    std::string gameId, gameName, title, type, thumbnailUrl, startedAt, language;
    int64_t     viewerCount = 0;
    int64_t     startedAtUnix = 0;
};

struct Vod {
    std::string id, streamId, userId, userLogin, userName;
    std::string title, description, createdAt, publishedAt, url, thumbnailUrl;
    std::string viewable, type, duration;   // type: archive | highlight | upload
    int64_t     viewCount     = 0;
    int64_t     createdAtUnix = 0;
    int         durationSecs  = 0;
};

struct FollowedChannel {
    std::string userId, login, displayName, followedAt;
};

// A Twitch category (called "game" by the API, but it covers Just Chatting,
// Music and the rest too).
struct Game {
    std::string id, name, boxArtUrl;
    int64_t     viewerCount = 0;   // only filled in by getTopGamesWithViewers
};

struct Clip {
    std::string id, url, embedUrl, videoId, gameId, language;
    std::string broadcasterId, broadcasterLogin, broadcasterName, creatorName;
    std::string title, thumbnailUrl, createdAt;
    int64_t     viewCount     = 0;
    int64_t     createdAtUnix = 0;
    double      duration      = 0.0;   // seconds
    int         vodOffset     = -1;    // -1 when the source VOD is gone
    bool        featured      = false;

    // Clips are served as a plain MP4 whose name follows the preview image.
    // Used as a fallback when the download endpoint gives nothing back.
    std::string guessMp4Url() const;
};

// One badge image, keyed by set ("subscriber") and version ("12").
struct BadgeVersion {
    std::string id, title;
    std::string url1x, url2x, url4x;
};
struct BadgeSet {
    std::string               setId;
    std::vector<BadgeVersion> versions;
};

// The parts of a room's chat configuration a viewer can see.
struct ChatSettings {
    bool slowMode        = false;
    int  slowModeWait    = 0;    // seconds
    bool followerMode    = false;
    int  followerMinutes = 0;
    bool subscriberMode  = false;
    bool emoteMode       = false;
    bool uniqueMode      = false;   // "r9k"
};

struct ChannelInfo {
    std::string broadcasterId, login, name, language;
    std::string gameId, gameName, title;
    int         delaySeconds = 0;
    std::vector<std::string> tags;
    std::vector<std::string> contentLabels;
    bool        brandedContent = false;
};

// Result wrapper so callers can distinguish "empty" from "failed".
template <typename T>
struct Result {
    bool        ok = false;
    T           value{};
    std::string error;
    int         status = 0;

    explicit operator bool() const { return ok; }
};

// Thin Helix client. Every call blocks, so run these on worker threads.
class Api {
public:
    Api(std::string clientId, std::string accessToken)
        : m_clientId(std::move(clientId)), m_token(std::move(accessToken)) {}

    void setToken(const std::string& t) { m_token = t; }
    void setClientId(const std::string& c) { m_clientId = c; }
    bool hasToken() const { return !m_token.empty(); }

    Result<User>                     getCurrentUser();
    Result<std::vector<User>>        getUsersByLogin(const std::vector<std::string>& logins);
    Result<std::vector<User>>        getUsersById(const std::vector<std::string>& ids);

    // Channels the signed-in user follows (paginates until exhausted).
    Result<std::vector<FollowedChannel>> getFollowedChannels(const std::string& userId);

    // Live streams among the followed channels.
    Result<std::vector<Stream>>      getFollowedStreams(const std::string& userId);
    Result<std::vector<Stream>>      getStreamsByUserId(const std::vector<std::string>& userIds);
    // `language` is a two-letter code ("pt", "en"); empty means every language.
    Result<std::vector<Stream>>      getTopStreams(int limit = 40,
                                                   const std::string& language = "");
    Result<std::vector<Stream>>      searchChannels(const std::string& query, int limit = 40,
                                                    bool liveOnly = false);

    // --- categories ---------------------------------------------------------
    // Most-watched categories right now, ordered by Twitch.
    Result<std::vector<Game>>        getTopGames(int limit = 40);
    // Categories whose name matches the query (this is what a game search needs).
    Result<std::vector<Game>>        searchCategories(const std::string& query, int limit = 40);
    Result<std::vector<Game>>        getGamesByName(const std::vector<std::string>& names);
    // Live streams currently in a category, most viewers first.
    Result<std::vector<Stream>>      getStreamsByGameId(const std::string& gameId, int limit = 60,
                                                        const std::string& language = "");

    // --- clips --------------------------------------------------------------
    // Most viewed clips for a channel. `days` limits how far back to look
    // (0 = all time).
    Result<std::vector<Clip>>        getClips(const std::string& broadcasterId,
                                              int limit = 60, int days = 0);
    // Official download link. Returns an empty string when the endpoint is
    // unavailable, in which case Clip::guessMp4Url() is the fallback.
    Result<std::string>              getClipDownloadUrl(const std::string& clipId);

    // --- chat presentation --------------------------------------------------
    Result<std::vector<BadgeSet>>    getGlobalBadges();
    Result<std::vector<BadgeSet>>    getChannelBadges(const std::string& broadcasterId);
    Result<ChatSettings>             getChatSettings(const std::string& broadcasterId);

    // --- channel ------------------------------------------------------------
    Result<ChannelInfo>              getChannelInfo(const std::string& broadcasterId);

    // `type` is archive/highlight/upload/all.
    Result<std::vector<Vod>>         getVideosByUser(const std::string& userId,
                                                     const std::string& type = "archive",
                                                     int limit = 100);
    Result<std::vector<Vod>>         getVideosById(const std::vector<std::string>& ids);

private:
    std::string m_clientId, m_token;
};

// Replaces the {width}/{height} tokens Twitch puts in thumbnail URLs.
std::string sizedThumb(const std::string& tmpl, int w, int h);

} // namespace dfts::twitch
