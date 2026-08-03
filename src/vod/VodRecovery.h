#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <functional>
#include <cstdint>

namespace dfts::vod {

// A VOD identity, however the user supplied it.
struct RecoveryQuery {
    std::string streamer;       // channel login, lowercase
    std::string streamId;       // numeric broadcast id
    int64_t     startTime = 0;  // unix seconds, UTC, of the broadcast start

    bool valid() const { return !streamer.empty() && !streamId.empty() && startTime > 0; }
};

struct RecoveredPlaylist {
    std::string url;            // .../<hash>_<base>/chunked/index-dvr.m3u8
    std::string domain;
    std::string quality;        // "chunked" (source) or a transcode folder
    int64_t     usedTimestamp = 0;
};

struct ScanProgress {
    int  checked = 0;
    int  total   = 0;
    bool done    = false;
};

// Builds the CDN path segment Twitch derives from the broadcast identity:
//   base = "<streamer>_<streamId>_<unixTime>"
//   path = first 20 hex chars of sha1(base) + "_" + base
std::string buildVodPath(const std::string& streamer, const std::string& streamId,
                         int64_t unixTime);

// Every known Twitch VOD CDN host the scanner probes.
const std::vector<std::string>& cdnDomains();

// Alternate rendition folder names to try once a domain answers.
const std::vector<std::string>& qualityFolders();

// Parses a Twitch Tracker or Sullygnome URL into a query. Returns false when
// the URL is not recognised. Twitch Tracker URLs carry the start time in the
// path; Sullygnome URLs only carry the ids, so `startTime` may stay zero.
bool parseTrackerUrl(const std::string& url, RecoveryQuery& out, std::string& note);

// Probes the CDN for a matching playlist. Tries `startTime` plus a window of
// neighbouring seconds because published start times are frequently rounded.
// Blocking; call from a worker thread. `cancel` aborts early.
std::vector<RecoveredPlaylist> scan(const RecoveryQuery& query,
                                    int secondsWindow,
                                    int threads,
                                    const std::function<void(ScanProgress)>& onProgress,
                                    const std::atomic<bool>* cancel);

// Given a found chunked playlist, lists the sibling renditions that also exist.
std::vector<RecoveredPlaylist> findAlternateQualities(const RecoveredPlaylist& found,
                                                      const std::atomic<bool>* cancel);

// --- muted segments ---------------------------------------------------------
// Twitch rewrites copyright-struck segments to "-muted.ts". The unmuted file is
// usually still on the CDN under its original name.
struct MutedInfo {
    int  mutedCount = 0;
    int  totalCount = 0;
};
MutedInfo countMutedSegments(const std::string& playlistBody);

// Returns a copy of the playlist with "-muted" stripped from segment names.
std::string unmutePlaylist(const std::string& playlistBody);

} // namespace dfts::vod
