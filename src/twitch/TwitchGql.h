#pragma once
#include <string>
#include <vector>

namespace dfts::twitch {

// One entry of an HLS master playlist.
struct Variant {
    std::string name;        // "1080p60", "720p", "audio_only", ...
    std::string groupId;
    std::string url;
    int         width      = 0;
    int         height     = 0;
    double      frameRate  = 0.0;
    int64_t     bandwidth  = 0;
};

struct PlaylistResult {
    bool                 ok = false;
    std::vector<Variant> variants;
    std::string          masterUrl;
    std::string          error;
    // HTTP status from the usher request, so callers can tell "restricted"
    // (403) from "gone" (404) instead of surfacing a bare number.
    int                  status = 0;

    // Picks the variant matching `preference` ("best", "worst", "audio_only",
    // "720p60", ...), falling back to the highest bandwidth stream.
    const Variant* pick(const std::string& preference) const;
};

// Resolves the playable HLS master playlist for a live channel or a VOD by
// requesting a playback access token from Twitch's GraphQL gateway and handing
// it to the usher service.
//
// These requests are anonymous on purpose. The gateway ties a bearer token to
// the Client-Id it was issued for, so sending this application's Helix token
// there returns 401. Anonymous tokens cover every public stream and VOD;
// subscriber-only content is the one thing they cannot reach.
PlaylistResult getLivePlaylist(const std::string& channelLogin);
PlaylistResult getVodPlaylist(const std::string& vodId);

// Parses an already-downloaded master playlist body.
PlaylistResult parseMasterPlaylist(const std::string& body, const std::string& masterUrl);

} // namespace dfts::twitch
