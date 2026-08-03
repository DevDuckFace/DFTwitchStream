#include "vod/VodRecovery.h"
#include "core/Sha1.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <algorithm>
#include <mutex>
#include <regex>
#include <thread>

namespace dfts::vod {

namespace {

// Serves the scan workers a shared index so every candidate is probed once.
struct WorkQueue {
    std::mutex mutex;
    size_t     next = 0;
};

} // namespace

std::string buildVodPath(const std::string& streamer, const std::string& streamId,
                         int64_t unixTime) {
    std::string base = util::lower(streamer) + "_" + streamId + "_" + std::to_string(unixTime);
    std::string hash = crypto::sha1Hex(base).substr(0, 20);
    return hash + "_" + base;
}

const std::vector<std::string>& cdnDomains() {
    // Twitch serves VOD storage from a set of regional buckets; a recovered
    // broadcast lives on exactly one of them, so all are probed.
    static const std::vector<std::string> domains = {
        "https://d2e2de1etea730.cloudfront.net",
        "https://dqrpb9wgowsf5.cloudfront.net",
        "https://ds0h3roq6wcgc.cloudfront.net",
        "https://d2nvs31859zcd8.cloudfront.net",
        "https://d2aba1wr3818hz.cloudfront.net",
        "https://d3c27h4odz752x.cloudfront.net",
        "https://dgeft87wbj63p.cloudfront.net",
        "https://d1m7jfoe9zdc1j.cloudfront.net",
        "https://d3vd9lfkzbru3h.cloudfront.net",
        "https://d2vjef5jvl6bfs.cloudfront.net",
        "https://d1ymi26ma8va5x.cloudfront.net",
        "https://d1mhjrowxxagfy.cloudfront.net",
        "https://ddacn6pr5v0tl.cloudfront.net",
        "https://d3aqoihi2n8ty8.cloudfront.net",
        "https://d1xhnb4ptk05mw.cloudfront.net",
        "https://d6d4ismr40iw.cloudfront.net",
        "https://d1oca24q5dwo6d.cloudfront.net",
        "https://d2um2qdswy1tb0.cloudfront.net",
        "https://d1w2poirtb3as9.cloudfront.net",
        "https://d6tizftlrpuof.cloudfront.net",
        "https://d36nr0u3xmc4mm.cloudfront.net",
        "https://d1eq9rc7hmfnwr.cloudfront.net",
        "https://d1qhlpx0e0refr.cloudfront.net",
        "https://d3kpuv44xstunr.cloudfront.net",
        "https://d1g1f25tn8m2e6.cloudfront.net",
        "https://d3fi1amfgojobc.cloudfront.net",
        "https://ds8xu8lswzhtb.cloudfront.net",
        "https://dykkng5hnh52u.cloudfront.net",
        "https://d1ymi26ma8va5x.cloudfront.net",
        "https://vod-secure.twitch.tv",
        "https://vod-metro.twitch.tv",
        "https://vod-pop-secure.twitch.tv",
    };
    return domains;
}

const std::vector<std::string>& qualityFolders() {
    static const std::vector<std::string> folders = {
        "chunked", "1080p60", "1080p30", "720p60", "720p30",
        "480p30", "360p30", "160p30", "audio_only",
    };
    return folders;
}

bool parseTrackerUrl(const std::string& url, RecoveryQuery& out, std::string& note) {
    note.clear();
    std::string u = util::trim(url);
    if (u.empty()) return false;

    // twitchtracker.com/<channel>/streams/<streamId>
    {
        static const std::regex re(R"(twitchtracker\.com/([^/\s?#]+)/streams/(\d+))",
                                   std::regex::icase);
        std::smatch m;
        if (std::regex_search(u, m, re)) {
            out.streamer = util::lower(m[1].str());
            out.streamId = m[2].str();
            note = "twitchtracker";
            return true;
        }
    }
    // sullygnome.com/channel/<channel>/stream/<streamId>
    {
        static const std::regex re(R"(sullygnome\.com/channel/([^/\s?#]+)/(?:\d+/)?stream/(\d+))",
                                   std::regex::icase);
        std::smatch m;
        if (std::regex_search(u, m, re)) {
            out.streamer = util::lower(m[1].str());
            out.streamId = m[2].str();
            note = "sullygnome";
            return true;
        }
    }
    // Plain "<channel> <streamId> <YYYY-MM-DD HH:MM:SS>" pasted by the user.
    {
        static const std::regex re(R"(^\s*(\S+)\s+(\d{6,})\s+(\d{4}-\d{2}-\d{2}[ T]\d{2}:\d{2}:\d{2}))");
        std::smatch m;
        if (std::regex_search(u, m, re)) {
            out.streamer  = util::lower(m[1].str());
            out.streamId  = m[2].str();
            std::string t = util::replaceAll(m[3].str(), " ", "T");
            out.startTime = util::parseIso8601(t);
            note = "manual";
            return true;
        }
    }
    return false;
}

std::vector<RecoveredPlaylist> scan(const RecoveryQuery& query, int secondsWindow, int threads,
                                    const std::function<void(ScanProgress)>& onProgress,
                                    const std::atomic<bool>* cancel) {
    std::vector<RecoveredPlaylist> results;
    if (!query.valid()) return results;

    secondsWindow = std::clamp(secondsWindow, 0, 300);
    threads       = std::clamp(threads, 1, 32);

    // Candidate timestamps ordered by distance from the reported start time,
    // so the most likely value is probed first.
    std::vector<int64_t> timestamps{query.startTime};
    for (int delta = 1; delta <= secondsWindow; ++delta) {
        timestamps.push_back(query.startTime - delta);
        timestamps.push_back(query.startTime + delta);
    }

    struct Candidate { std::string url; std::string domain; int64_t ts; };
    std::vector<Candidate> candidates;
    candidates.reserve(timestamps.size() * cdnDomains().size());
    for (int64_t ts : timestamps) {
        std::string path = buildVodPath(query.streamer, query.streamId, ts);
        for (const auto& domain : cdnDomains())
            candidates.push_back({domain + "/" + path + "/chunked/index-dvr.m3u8", domain, ts});
    }

    LOG_I("recovery: probing " + std::to_string(candidates.size()) + " candidate URLs for " +
          query.streamer + "/" + query.streamId);

    WorkQueue queue;
    std::mutex resultMutex;
    std::atomic<int> checked{0};
    std::atomic<bool> found{false};

    auto worker = [&]() {
        for (;;) {
            if ((cancel && cancel->load()) || found.load()) return;

            size_t index;
            {
                std::lock_guard<std::mutex> lock(queue.mutex);
                if (queue.next >= candidates.size()) return;
                index = queue.next++;
            }

            const Candidate& c = candidates[index];
            bool exists = http::urlExists(c.url, 6000);
            int done = ++checked;
            if (onProgress) onProgress({done, (int)candidates.size(), false});

            if (exists) {
                std::lock_guard<std::mutex> lock(resultMutex);
                results.push_back({c.url, c.domain, "chunked", c.ts});
                found = true;   // one hit is enough; siblings come from findAlternateQualities
                LOG_I("recovery: hit " + c.url);
                return;
            }
        }
    };

    std::vector<std::thread> pool;
    for (int i = 0; i < threads; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    if (onProgress) onProgress({checked.load(), (int)candidates.size(), true});
    return results;
}

std::vector<RecoveredPlaylist> findAlternateQualities(const RecoveredPlaylist& found,
                                                      const std::atomic<bool>* cancel) {
    std::vector<RecoveredPlaylist> out;

    // Strip "/chunked/index-dvr.m3u8" to get the VOD root on the CDN.
    size_t pos = found.url.rfind("/chunked/");
    if (pos == std::string::npos) {
        pos = found.url.find_last_of('/');
        if (pos == std::string::npos) return out;
        pos = found.url.find_last_of('/', pos - 1);
    }
    std::string root = found.url.substr(0, pos);

    std::mutex mutex;
    std::vector<std::thread> pool;
    for (const auto& folder : qualityFolders()) {
        pool.emplace_back([&, folder]() {
            if (cancel && cancel->load()) return;
            std::string url = root + "/" + folder + "/index-dvr.m3u8";
            if (!http::urlExists(url, 6000)) return;
            std::lock_guard<std::mutex> lock(mutex);
            out.push_back({url, found.domain, folder, found.usedTimestamp});
        });
    }
    for (auto& t : pool) t.join();

    // Present source first, then descending resolution.
    const auto& order = qualityFolders();
    std::sort(out.begin(), out.end(), [&](const RecoveredPlaylist& a, const RecoveredPlaylist& b) {
        auto rank = [&](const std::string& q) {
            auto it = std::find(order.begin(), order.end(), q);
            return it == order.end() ? order.size() : (size_t)(it - order.begin());
        };
        return rank(a.quality) < rank(b.quality);
    });
    return out;
}

MutedInfo countMutedSegments(const std::string& playlistBody) {
    MutedInfo info;
    for (const auto& raw : util::split(playlistBody, '\n')) {
        std::string line = util::trim(raw);
        if (line.empty() || line[0] == '#') continue;
        ++info.totalCount;
        if (util::icontains(line, "-muted")) ++info.mutedCount;
    }
    return info;
}

std::string unmutePlaylist(const std::string& playlistBody) {
    std::string out;
    out.reserve(playlistBody.size());
    for (const auto& raw : util::split(playlistBody, '\n')) {
        std::string line = raw;
        if (!util::trim(line).empty() && util::trim(line)[0] != '#')
            line = util::replaceAll(line, "-muted", "");
        out += line;
        out += '\n';
    }
    return out;
}

} // namespace dfts::vod
