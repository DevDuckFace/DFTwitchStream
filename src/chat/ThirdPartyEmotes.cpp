#include "chat/ThirdPartyEmotes.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <nlohmann/json.hpp>

using nlohmann::json;

namespace dfts::chat {

namespace {

json fetchJson(const std::string& url, std::string& error) {
    http::Response r = http::get(url, {{"Accept", "application/json"}});
    if (r.status == 0) { error = r.error; return json(); }
    if (!r.ok())       { error = "HTTP " + std::to_string(r.status); return json(); }
    try {
        return json::parse(r.body);
    } catch (const std::exception& e) {
        error = std::string("bad json: ") + e.what();
        return json();
    }
}

// Some services return protocol-relative URLs ("//cdn...").
std::string absoluteUrl(std::string url) {
    if (util::startsWith(url, "//")) return "https:" + url;
    return url;
}

// --- BetterTTV --------------------------------------------------------------
// Emote objects carry an id and a code; the image lives at a fixed CDN path.
// Requesting the plain size (no extension) yields PNG or GIF, which the
// Windows imaging component can decode without extra codecs installed.
void absorbBttvList(const json& list, EmoteTable& out) {
    if (!list.is_array()) return;
    for (const auto& e : list) {
        const std::string id   = e.value("id", "");
        const std::string code = e.value("code", "");
        if (id.empty() || code.empty()) continue;

        ThirdPartyEmote emote;
        emote.name     = code;
        emote.url      = "https://cdn.betterttv.net/emote/" + id + "/1x";
        emote.provider = "BTTV";
        emote.animated = e.value("animated", false) ||
                         util::lower(e.value("imageType", "")) == "gif";
        out[code] = std::move(emote);
    }
}

// --- FrankerFaceZ -----------------------------------------------------------
// Sets hold emoticons whose `urls` map is keyed by scale ("1", "2", "4").
void absorbFfzSets(const json& sets, EmoteTable& out) {
    if (!sets.is_object()) return;
    for (const auto& [setId, set] : sets.items()) {
        if (!set.contains("emoticons") || !set["emoticons"].is_array()) continue;

        for (const auto& e : set["emoticons"]) {
            const std::string name = e.value("name", "");
            if (name.empty()) continue;

            std::string url;
            if (e.contains("urls") && e["urls"].is_object()) {
                const json& urls = e["urls"];
                for (const char* scale : {"1", "2", "4"}) {
                    if (urls.contains(scale) && urls[scale].is_string()) {
                        url = urls[scale].get<std::string>();
                        break;
                    }
                }
            }
            if (url.empty()) continue;

            ThirdPartyEmote emote;
            emote.name     = name;
            emote.url      = absoluteUrl(url);
            emote.provider = "FFZ";
            out[name] = std::move(emote);
        }
    }
}

// --- 7TV --------------------------------------------------------------------
// The host object gives a base path plus a list of files in several formats.
// PNG and GIF are preferred: WebP and AVIF need image extensions that are not
// present on every Windows install, and would silently fail to decode.
std::string pick7tvFile(const json& host) {
    if (!host.is_object()) return {};
    const std::string base = absoluteUrl(host.value("url", ""));
    if (base.empty()) return {};

    std::string best;
    int bestRank = 99;
    if (host.contains("files") && host["files"].is_array()) {
        for (const auto& file : host["files"]) {
            const std::string name = file.value("name", "");
            if (name.empty()) continue;
            // Only the smallest size is worth fetching for chat.
            if (!util::startsWith(name, "1x")) continue;

            const std::string lower = util::lower(name);
            int rank = 99;
            if      (util::endsWith(lower, ".png"))  rank = 0;
            else if (util::endsWith(lower, ".gif"))  rank = 1;
            else if (util::endsWith(lower, ".webp")) rank = 2;
            else if (util::endsWith(lower, ".avif")) rank = 3;

            if (rank < bestRank) { bestRank = rank; best = name; }
        }
    }
    if (best.empty()) return base + "/1x.webp";   // last resort
    return base + "/" + best;
}

void absorb7tvList(const json& list, EmoteTable& out) {
    if (!list.is_array()) return;
    for (const auto& e : list) {
        const std::string name = e.value("name", "");
        if (name.empty()) continue;
        if (!e.contains("data") || !e["data"].is_object()) continue;

        const json& data = e["data"];
        const std::string url = data.contains("host") ? pick7tvFile(data["host"])
                                                      : std::string();
        if (url.empty()) continue;

        ThirdPartyEmote emote;
        emote.name     = name;
        emote.url      = url;
        emote.provider = "7TV";
        emote.animated = data.value("animated", false);
        out[name] = std::move(emote);
    }
}

} // namespace

void mergeEmotes(EmoteTable& base, const EmoteTable& extra) {
    for (const auto& [name, emote] : extra) base[name] = emote;
}

EmoteTable fetchGlobalEmotes() {
    EmoteTable table;
    std::string error;

    // FFZ first, then BTTV, then 7TV: later providers win on a name clash,
    // matching what the popular browser extensions do.
    if (json j = fetchJson("https://api.frankerfacez.com/v1/set/global", error);
        j.is_object() && j.contains("sets")) {
        absorbFfzSets(j["sets"], table);
    } else if (!error.empty()) {
        LOG_W("FFZ global emotes: " + error);
    }

    error.clear();
    if (json j = fetchJson("https://api.betterttv.net/3/cached/emotes/global", error);
        j.is_array()) {
        absorbBttvList(j, table);
    } else if (!error.empty()) {
        LOG_W("BTTV global emotes: " + error);
    }

    error.clear();
    if (json j = fetchJson("https://7tv.io/v3/emote-sets/global", error);
        j.is_object() && j.contains("emotes")) {
        absorb7tvList(j["emotes"], table);
    } else if (!error.empty()) {
        LOG_W("7TV global emotes: " + error);
    }

    LOG_I("third-party global emotes loaded: " + std::to_string(table.size()));
    return table;
}

EmoteTable fetchChannelEmotes(const std::string& twitchUserId) {
    EmoteTable table;
    if (twitchUserId.empty()) return table;
    std::string error;

    if (json j = fetchJson("https://api.frankerfacez.com/v1/room/id/" + twitchUserId, error);
        j.is_object() && j.contains("sets")) {
        absorbFfzSets(j["sets"], table);
    } else if (!error.empty()) {
        LOG_D("FFZ channel emotes: " + error);   // channels often have none
    }

    error.clear();
    if (json j = fetchJson("https://api.betterttv.net/3/cached/users/twitch/" + twitchUserId,
                           error);
        j.is_object()) {
        // Channel emotes belong to the broadcaster, shared ones are borrowed
        // from the BTTV library; chat treats them the same.
        if (j.contains("channelEmotes")) absorbBttvList(j["channelEmotes"], table);
        if (j.contains("sharedEmotes"))  absorbBttvList(j["sharedEmotes"], table);
    } else if (!error.empty()) {
        LOG_D("BTTV channel emotes: " + error);
    }

    error.clear();
    if (json j = fetchJson("https://7tv.io/v3/users/twitch/" + twitchUserId, error);
        j.is_object() && j.contains("emote_set") && j["emote_set"].is_object()) {
        const json& set = j["emote_set"];
        if (set.contains("emotes")) absorb7tvList(set["emotes"], table);
    } else if (!error.empty()) {
        LOG_D("7TV channel emotes: " + error);
    }

    LOG_I("third-party channel emotes loaded: " + std::to_string(table.size()));
    return table;
}

} // namespace dfts::chat
