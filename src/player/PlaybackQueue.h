#pragma once
#include <functional>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>

namespace dfts::player {

// Serialises player start/stop on a dedicated thread so the UI never blocks.
//
// Starting media is slow: libvlc_media_player_stop() waits for the decoder to
// unwind and opening a network stream costs a round trip. Doing that on the
// render thread freezes the window, which is exactly what happens when the
// user flicks through channels.
//
// Only the most recent request matters, so a pending request is overwritten
// rather than queued: switching through ten channels starts one stream, not
// ten in sequence.
class PlaybackQueue {
public:
    struct Request {
        bool        stop  = false;   // true => stop playback, ignore the rest
        std::string url;
        std::string title;
        bool        live  = false;
        int         maxHeight = 0;   // rendition ceiling for master playlists
        uint64_t    generation = 0;  // lets the executor drop superseded work
    };

    // `executor` runs on the worker thread.
    using Executor = std::function<void(const Request&)>;

    ~PlaybackQueue();

    void start(Executor executor);
    void shutdown();

    // Replaces any request that has not started yet.
    void submit(Request request);

    // Generation of the newest request seen, for staleness checks.
    uint64_t currentGeneration() const { return m_generation.load(); }

    // True while the worker is inside the executor.
    bool busy() const { return m_busy.load(); }

private:
    void run();

    Executor                m_executor;
    std::thread             m_thread;
    std::mutex              m_mutex;
    std::condition_variable m_cv;
    Request                 m_pending;
    bool                    m_hasPending = false;
    std::atomic<bool>       m_running{false};
    std::atomic<bool>       m_busy{false};
    std::atomic<uint64_t>   m_generation{0};
};

} // namespace dfts::player
