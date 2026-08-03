#include "vod/Downloader.h"
#include "vod/VodRecovery.h"   // unmutePlaylist
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"
#include "core/Process.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>

namespace fs = std::filesystem;

namespace dfts::vod {

// ---------------------------------------------------------------------------
// Playlist parsing
// ---------------------------------------------------------------------------
std::vector<std::string> parseMediaPlaylist(const std::string& body, const std::string& baseUrl) {
    std::vector<std::string> out;
    for (const auto& raw : util::split(body, '\n')) {
        std::string line = util::trim(raw);
        if (line.empty() || line[0] == '#') continue;
        out.push_back(http::resolveRelative(baseUrl, line));
    }
    return out;
}

double playlistDuration(const std::string& body) {
    double total = 0.0;
    for (const auto& raw : util::split(body, '\n')) {
        std::string line = util::trim(raw);
        if (!util::startsWith(line, "#EXTINF:")) continue;
        total += std::strtod(line.c_str() + 8, nullptr);
    }
    return total;
}

// ---------------------------------------------------------------------------
// Job
// ---------------------------------------------------------------------------
struct DownloadManager::Job {
    JobRequest        request;
    JobStatus         status;
    std::atomic<bool> cancel{false};
    std::thread       thread;
    mutable std::mutex mutex;

    JobStatus snapshot() const {
        std::lock_guard<std::mutex> lock(mutex);
        return status;
    }
    void update(const std::function<void(JobStatus&)>& fn) {
        std::lock_guard<std::mutex> lock(mutex);
        fn(status);
    }
};

DownloadManager& DownloadManager::get() {
    static DownloadManager instance;
    return instance;
}

DownloadManager::~DownloadManager() {
    cancelAll();
    std::vector<std::shared_ptr<Job>> jobs;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        jobs = m_jobs;
    }
    for (auto& j : jobs)
        if (j->thread.joinable()) j->thread.join();
}

int DownloadManager::enqueue(const JobRequest& req) {
    auto job = std::make_shared<Job>();
    job->request       = req;
    job->status.title  = req.title.empty() ? "vod" : req.title;
    job->status.state  = JobState::Queued;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        job->status.id = m_nextId++;
        m_jobs.push_back(job);
    }
    job->thread = std::thread(&DownloadManager::runJob, this, job);
    LOG_I("download queued: " + job->status.title);
    return job->status.id;
}

void DownloadManager::cancel(int jobId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs)
        if (j->status.id == jobId) j->cancel = true;
}

void DownloadManager::cancelAll() {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (auto& j : m_jobs) j->cancel = true;
}

void DownloadManager::clearFinished() {
    std::vector<std::shared_ptr<Job>> removed;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        for (auto it = m_jobs.begin(); it != m_jobs.end();) {
            if ((*it)->snapshot().finished()) {
                removed.push_back(*it);
                it = m_jobs.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& j : removed)
        if (j->thread.joinable()) j->thread.join();
}

std::vector<JobStatus> DownloadManager::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<JobStatus> out;
    out.reserve(m_jobs.size());
    for (const auto& j : m_jobs) out.push_back(j->snapshot());
    return out;
}

bool DownloadManager::anyActive() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    for (const auto& j : m_jobs)
        if (!j->snapshot().finished()) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Worker
// ---------------------------------------------------------------------------
void DownloadManager::runJob(std::shared_ptr<Job> job) {
    const JobRequest& req = job->request;

    auto fail = [&](const std::string& msg) {
        job->update([&](JobStatus& s) {
            s.state   = JobState::Failed;
            s.message = msg;
        });
        LOG_E("download failed: " + msg);
    };

    // --- single media file (a clip): no playlist to walk -------------------
    if (req.singleFile) {
        std::string dir = req.outputDir.empty() ? util::defaultDownloadDir() : req.outputDir;
        util::ensureDir(dir);

        const std::string stem = util::sanitizeFileName(req.title.empty() ? "clip" : req.title);
        const std::string dest = dir + "\\" + stem + ".mp4";

        job->update([&](JobStatus& s) {
            s.state         = JobState::Downloading;
            s.segmentsTotal = 1;
            s.outputPath    = dest;
        });

        const bool ok = http::download(
            req.playlistUrl, dest, {{"Referer", "https://www.twitch.tv/"}},
            [&job](uint64_t got, uint64_t total) {
                job->update([&](JobStatus& s) {
                    s.bytes    = got;
                    s.progress = total > 0 ? (float)((double)got / (double)total) : 0.0f;
                });
                return true;
            },
            &job->cancel);

        if (job->cancel.load()) {
            job->update([](JobStatus& s) { s.state = JobState::Cancelled; });
        } else if (!ok) {
            fail("clip download failed");
        } else {
            job->update([](JobStatus& s) {
                s.state         = JobState::Done;
                s.segmentsDone  = 1;
                s.progress      = 1.0f;
            });
            LOG_I("clip downloaded: " + dest);
        }
        return;
    }

    // --- resolve the media playlist ---------------------------------------
    job->update([](JobStatus& s) { s.state = JobState::FetchingPlaylist; });

    http::Response pl = http::get(req.playlistUrl, {{"Referer", "https://www.twitch.tv/"}});
    if (!pl.ok()) {
        fail(pl.status ? ("playlist HTTP " + std::to_string(pl.status)) : pl.error);
        return;
    }

    std::string body = pl.body;
    if (req.unmute) body = unmutePlaylist(body);

    std::vector<std::string> segments = parseMediaPlaylist(body, req.playlistUrl);
    if (segments.empty()) { fail("playlist has no segments"); return; }

    // Optional trim, using the average segment length as the mapping.
    if (req.startOffset > 0 || req.endOffset > 0) {
        const double total = playlistDuration(body);
        const double per   = total > 0 ? total / (double)segments.size() : 10.0;
        const size_t first = (size_t)std::max(0.0, (double)req.startOffset / per);
        const size_t last  = req.endOffset > 0
                           ? (size_t)std::min((double)segments.size(),
                                              (double)req.endOffset / per)
                           : segments.size();
        if (first < last && last <= segments.size())
            segments = std::vector<std::string>(segments.begin() + first, segments.begin() + last);
    }

    std::string dir = req.outputDir.empty() ? util::defaultDownloadDir() : req.outputDir;
    util::ensureDir(dir);

    std::string stem     = util::sanitizeFileName(req.title.empty() ? "vod" : req.title);
    std::string workDir  = dir + "\\." + stem + "_parts";
    util::ensureDir(workDir);

    job->update([&](JobStatus& s) {
        s.state         = JobState::Downloading;
        s.segmentsTotal = (int)segments.size();
        s.outputPath    = dir + "\\" + stem + (req.muxToMp4 ? ".mp4" : ".ts");
    });

    // --- download segments in parallel -------------------------------------
    std::vector<std::string> partFiles(segments.size());
    std::atomic<size_t> nextIndex{0};
    std::atomic<int>    completed{0};
    std::atomic<uint64_t> totalBytes{0};
    std::atomic<bool>   hardFail{false};

    auto started = std::chrono::steady_clock::now();

    auto worker = [&]() {
        for (;;) {
            if (job->cancel.load() || hardFail.load()) return;
            size_t i = nextIndex.fetch_add(1);
            if (i >= segments.size()) return;

            char name[64];
            std::snprintf(name, sizeof(name), "seg_%06zu.ts", i);
            std::string dest = workDir + "\\" + name;
            partFiles[i] = dest;

            // Skip work already done by an interrupted run of the same job.
            std::error_code ec;
            if (fs::exists(util::widen(dest), ec) && fs::file_size(util::widen(dest), ec) > 0) {
                totalBytes += fs::file_size(util::widen(dest), ec);
            } else {
                bool ok = false;
                for (int attempt = 0; attempt < 3 && !ok; ++attempt) {
                    if (job->cancel.load()) return;
                    ok = http::download(segments[i], dest, {{"Referer", "https://www.twitch.tv/"}},
                                        {}, &job->cancel);
                    if (!ok) std::this_thread::sleep_for(std::chrono::milliseconds(400 * (attempt + 1)));
                }
                if (!ok) {
                    // A muted segment that no longer exists unmuted is common;
                    // fall back to the muted name before giving up on the file.
                    std::string fallback = util::replaceAll(segments[i], ".ts", "-muted.ts");
                    ok = http::download(fallback, dest, {{"Referer", "https://www.twitch.tv/"}},
                                        {}, &job->cancel);
                }
                if (!ok) {
                    LOG_W("segment failed, skipping: " + segments[i]);
                    partFiles[i].clear();
                } else {
                    totalBytes += fs::file_size(util::widen(dest), ec);
                }
            }

            int done = ++completed;
            double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            job->update([&](JobStatus& s) {
                s.segmentsDone = done;
                s.bytes        = totalBytes.load();
                s.bytesPerSec  = secs > 0.1 ? (double)totalBytes.load() / secs : 0.0;
                s.progress     = s.segmentsTotal ? (float)done / (float)s.segmentsTotal : 0.0f;
            });
        }
    };

    int threadCount = std::clamp(req.threads, 1, 32);
    std::vector<std::thread> pool;
    for (int i = 0; i < threadCount; ++i) pool.emplace_back(worker);
    for (auto& t : pool) t.join();

    if (job->cancel.load()) {
        job->update([](JobStatus& s) { s.state = JobState::Cancelled; });
        std::error_code ec;
        if (!req.keepSegments) fs::remove_all(util::widen(workDir), ec);
        return;
    }

    // --- concatenate -------------------------------------------------------
    std::string tsPath = dir + "\\" + stem + ".ts";
    {
        std::ofstream out(util::widen(tsPath), std::ios::binary | std::ios::trunc);
        if (!out) { fail("cannot write " + tsPath); return; }
        std::vector<char> buf(1 << 20);
        for (const auto& part : partFiles) {
            if (part.empty()) continue;
            std::ifstream in(util::widen(part), std::ios::binary);
            if (!in) continue;
            while (in.read(buf.data(), (std::streamsize)buf.size()) || in.gcount() > 0)
                out.write(buf.data(), in.gcount());
        }
    }

    std::error_code ec;
    if (!req.keepSegments) fs::remove_all(util::widen(workDir), ec);

    // --- optional mux to mp4 ----------------------------------------------
    if (!req.muxToMp4) {
        job->update([&](JobStatus& s) {
            s.state      = JobState::Done;
            s.outputPath = tsPath;
            s.progress   = 1.0f;
        });
        LOG_I("download finished: " + tsPath);
        return;
    }

    std::string ffmpeg = req.ffmpegPath.empty() ? util::findOnPath("ffmpeg") : req.ffmpegPath;
    if (ffmpeg.empty() || !util::fileExists(ffmpeg)) {
        job->update([&](JobStatus& s) {
            s.state      = JobState::Done;
            s.outputPath = tsPath;
            s.message    = "ffmpeg not found - kept as .ts";
            s.progress   = 1.0f;
        });
        LOG_W("ffmpeg not found; kept " + tsPath);
        return;
    }

    job->update([](JobStatus& s) { s.state = JobState::Muxing; });
    std::string mp4Path = dir + "\\" + stem + ".mp4";
    std::string output;
    // Stream copy only: remuxing the container is fast and lossless.
    int rc = proc::runCapture(ffmpeg, {"-y", "-hide_banner", "-loglevel", "error",
                                       "-i", tsPath, "-c", "copy",
                                       "-bsf:a", "aac_adtstoasc", mp4Path},
                              output, 3600000);
    if (rc != 0) {
        LOG_W("ffmpeg exit " + std::to_string(rc) + ": " + output);
        job->update([&](JobStatus& s) {
            s.state      = JobState::Done;
            s.outputPath = tsPath;
            s.message    = "ffmpeg failed - kept as .ts";
            s.progress   = 1.0f;
        });
        return;
    }

    fs::remove(util::widen(tsPath), ec);
    job->update([&](JobStatus& s) {
        s.state      = JobState::Done;
        s.outputPath = mp4Path;
        s.progress   = 1.0f;
    });
    LOG_I("download finished: " + mp4Path);
}

} // namespace dfts::vod
