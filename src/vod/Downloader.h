#pragma once
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <deque>
#include <cstdint>

namespace dfts::vod {

enum class JobState { Queued, FetchingPlaylist, Downloading, Muxing, Done, Failed, Cancelled };

// A snapshot of a job, safe to read from the UI thread.
struct JobStatus {
    int         id = 0;
    JobState    state = JobState::Queued;
    std::string title;
    std::string outputPath;
    std::string message;
    int         segmentsDone  = 0;
    int         segmentsTotal = 0;
    uint64_t    bytes         = 0;
    double      bytesPerSec   = 0.0;
    float       progress      = 0.0f;   // 0..1

    bool finished() const {
        return state == JobState::Done || state == JobState::Failed || state == JobState::Cancelled;
    }
};

struct JobRequest {
    std::string playlistUrl;    // media playlist (index-dvr.m3u8) or master playlist
    std::string title;          // used for the output file name
    std::string outputDir;
    std::string ffmpegPath;     // "" => auto-detect
    int         threads       = 8;
    bool        muxToMp4      = true;
    bool        keepSegments  = false;
    bool        unmute        = true;   // rewrite "-muted" segment names
    // `playlistUrl` is a complete media file (a clip) rather than a playlist:
    // fetch it straight to disk with no segment handling.
    bool        singleFile    = false;
    // Optional trimming, in seconds from the start. 0/0 means the whole VOD.
    int         startOffset   = 0;
    int         endOffset     = 0;
};

// Runs download jobs on background threads; the UI polls snapshot().
class DownloadManager {
public:
    static DownloadManager& get();
    ~DownloadManager();

    int  enqueue(const JobRequest& req);
    void cancel(int jobId);
    void cancelAll();
    void clearFinished();

    std::vector<JobStatus> snapshot() const;
    bool anyActive() const;

private:
    struct Job;
    DownloadManager() = default;

    void runJob(std::shared_ptr<Job> job);

    mutable std::mutex m_mutex;
    std::vector<std::shared_ptr<Job>> m_jobs;
    int m_nextId = 1;
};

// Parses an HLS media playlist into absolute segment URLs.
std::vector<std::string> parseMediaPlaylist(const std::string& body, const std::string& baseUrl);

// Total duration in seconds implied by the #EXTINF tags.
double playlistDuration(const std::string& body);

} // namespace dfts::vod
