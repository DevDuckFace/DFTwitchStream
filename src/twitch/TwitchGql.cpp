#include "twitch/TwitchGql.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <algorithm>
#include <cstdio>
#include <nlohmann/json.hpp>

using nlohmann::json;

namespace dfts::twitch {

namespace {

constexpr const char* kGqlUrl    = "https://gql.twitch.tv/gql";
constexpr const char* kUsherLive = "https://usher.ttvnw.net/api/channel/hls/";
constexpr const char* kUsherVod  = "https://usher.ttvnw.net/vod/";

// Client-Id of Twitch's own web player. The playback-token endpoint only
// accepts requests carrying a first-party client id.
constexpr const char* kWebClientId = "kimne78kx3ncx6brgo4mv6wki5h1ko";

// Requests a signature/token pair for either a live channel or a VOD.
//
// Deliberately anonymous. The playback-token endpoint validates that the bearer
// token was issued for the Client-Id sent alongside it, and our Helix token
// belongs to this application's own client id -- pairing the two is exactly
// what makes Twitch answer 401. Anonymous tokens cover all public content.
bool requestPlaybackToken(const std::string& login, bool isLive,
                          std::string& outToken, std::string& outSig, std::string& error) {
    json payload = {
        {"operationName", "PlaybackAccessToken_Template"},
        {"query",
         "query PlaybackAccessToken_Template("
         "$login: String!, $isLive: Boolean!, $vodID: ID!, $isVod: Boolean!, $playerType: String!) {"
         "  streamPlaybackAccessToken(channelName: $login, params: {platform: \"web\", "
         "playerBackend: \"mediaplayer\", playerType: $playerType}) @include(if: $isLive) "
         "{ value signature __typename }"
         "  videoPlaybackAccessToken(id: $vodID, params: {platform: \"web\", "
         "playerBackend: \"mediaplayer\", playerType: $playerType}) @include(if: $isVod) "
         "{ value signature __typename }"
         "}"},
        {"variables", {
            {"isLive",     isLive},
            {"login",      isLive ? login : ""},
            {"isVod",      !isLive},
            {"vodID",      isLive ? "" : login},
            {"playerType", "site"},
        }},
    };

    http::Headers headers{
        {"Client-Id", kWebClientId},
        {"Referer",   "https://www.twitch.tv/"},
        {"Origin",    "https://www.twitch.tv"},
    };

    http::Response r = http::post(kGqlUrl, payload.dump(), headers, "application/json");
    if (r.status == 0) { error = r.error; return false; }
    if (!r.ok()) {
        error = "GQL HTTP " + std::to_string(r.status);
        if (r.status == 401 || r.status == 403)
            error += " (Twitch rejected the playback token request)";
        return false;
    }

    try {
        json j = json::parse(r.body);
        const json& node = isLive ? j["data"]["streamPlaybackAccessToken"]
                                  : j["data"]["videoPlaybackAccessToken"];
        if (node.is_null()) { error = "no playback token (channel offline or video unavailable)"; return false; }
        outToken = node.value("value", "");
        outSig   = node.value("signature", "");
    } catch (const std::exception& e) {
        error = std::string("GQL parse error: ") + e.what();
        return false;
    }
    return !outToken.empty() && !outSig.empty();
}

std::string buildUsherUrl(const std::string& base, const std::string& id,
                          const std::string& token, const std::string& sig) {
    // `p` randomises the request so intermediate caches do not serve a stale
    // playlist for a stream that just changed variants.
    return base + id + ".m3u8"
           "?client_id="   + std::string(kWebClientId) +
           "&token="       + util::urlEncode(token) +
           "&sig="         + util::urlEncode(sig) +
           "&allow_source=true&allow_audio_only=true&fast_bread=true"
           "&playlist_include_framerate=true&reassignments_supported=true"
           "&supported_codecs=h264&p=" + std::to_string(std::rand() % 9000000 + 1000000);
}

// Extracts ATTR="value" or ATTR=value from an EXT-X tag line.
std::string attr(const std::string& line, const std::string& key) {
    size_t pos = line.find(key + "=");
    if (pos == std::string::npos) return {};
    pos += key.size() + 1;
    if (pos < line.size() && line[pos] == '"') {
        size_t end = line.find('"', pos + 1);
        if (end == std::string::npos) return {};
        return line.substr(pos + 1, end - pos - 1);
    }
    size_t end = line.find(',', pos);
    return line.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
}

} // namespace

const Variant* PlaylistResult::pick(const std::string& preference) const {
    if (variants.empty()) return nullptr;

    auto byBandwidth = [](const Variant& a, const Variant& b) { return a.bandwidth < b.bandwidth; };

    if (preference.empty() || preference == "best") {
        // "chunked" is the source rendition and may report a lower bitrate than
        // a transcode, so prefer it explicitly when present.
        for (const auto& v : variants)
            if (v.groupId == "chunked" || v.name == "chunked") return &v;
        return &*std::max_element(variants.begin(), variants.end(), byBandwidth);
    }
    if (preference == "worst")
        return &*std::min_element(variants.begin(), variants.end(), byBandwidth);

    for (const auto& v : variants)
        if (util::lower(v.name) == util::lower(preference)) return &v;

    // "720p" should still match "720p60" when no exact rendition exists.
    for (const auto& v : variants)
        if (util::startsWith(util::lower(v.name), util::lower(preference))) return &v;

    return &*std::max_element(variants.begin(), variants.end(), byBandwidth);
}

PlaylistResult parseMasterPlaylist(const std::string& body, const std::string& masterUrl) {
    PlaylistResult out;
    out.masterUrl = masterUrl;

    std::vector<std::string> lines = util::split(body, '\n');
    std::string pendingName, pendingGroup;

    for (size_t i = 0; i < lines.size(); ++i) {
        std::string line = util::trim(lines[i]);
        if (line.empty()) continue;

        if (util::startsWith(line, "#EXT-X-MEDIA:")) {
            pendingName  = attr(line, "NAME");
            pendingGroup = attr(line, "GROUP-ID");
            continue;
        }
        if (!util::startsWith(line, "#EXT-X-STREAM-INF:")) continue;

        Variant v;
        v.name      = pendingName.empty() ? attr(line, "VIDEO") : pendingName;
        v.groupId   = pendingGroup.empty() ? attr(line, "VIDEO") : pendingGroup;
        v.bandwidth = std::strtoll(attr(line, "BANDWIDTH").c_str(), nullptr, 10);
        v.frameRate = std::strtod(attr(line, "FRAME-RATE").c_str(), nullptr);

        std::string res = attr(line, "RESOLUTION");
        if (!res.empty()) sscanf_s(res.c_str(), "%dx%d", &v.width, &v.height);

        // The URI is the first non-comment line after the tag.
        for (size_t k = i + 1; k < lines.size(); ++k) {
            std::string uri = util::trim(lines[k]);
            if (uri.empty() || uri[0] == '#') continue;
            v.url = http::resolveRelative(masterUrl, uri);
            i = k;
            break;
        }
        pendingName.clear();
        pendingGroup.clear();

        if (v.name.empty()) {
            v.name = v.height ? (std::to_string(v.height) + "p") : "unknown";
            if (v.frameRate >= 50.0) v.name += std::to_string((int)std::lround(v.frameRate));
        }
        if (!v.url.empty()) out.variants.push_back(std::move(v));
    }

    out.ok = !out.variants.empty();
    if (!out.ok) out.error = "master playlist contained no variants";
    return out;
}

static PlaylistResult fetchPlaylist(const std::string& id, bool isLive) {
    PlaylistResult out;
    std::string token, sig;
    if (!requestPlaybackToken(id, isLive, token, sig, out.error)) return out;

    std::string url = buildUsherUrl(isLive ? kUsherLive : kUsherVod, id, token, sig);
    http::Response r = http::get(url, {{"Referer", "https://www.twitch.tv/"}});
    out.status = r.status;
    if (r.status == 0) { out.error = r.error; return out; }
    if (!r.ok()) {
        // 403 is what a subscriber-only or region-locked item returns to an
        // anonymous token; 404 means it is simply not there any more.
        out.error = "usher HTTP " + std::to_string(r.status);
        if (r.status == 404) out.error += isLive ? " (channel offline)" : " (video not available)";
        if (r.status == 403) out.error += " (restricted content)";
        return out;
    }

    PlaylistResult parsed = parseMasterPlaylist(r.body, url);
    parsed.status = r.status;
    return parsed;
}

PlaylistResult getLivePlaylist(const std::string& channelLogin) {
    LOG_I("resolving live playlist for " + channelLogin);
    return fetchPlaylist(util::lower(util::trim(channelLogin)), true);
}

PlaylistResult getVodPlaylist(const std::string& vodId) {
    LOG_I("resolving VOD playlist for " + vodId);
    std::string id = util::trim(vodId);
    if (!id.empty() && id[0] == 'v') id.erase(0, 1);   // accept "v123456789"
    return fetchPlaylist(id, false);
}

} // namespace dfts::twitch
