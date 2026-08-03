#include "player/PlaybackQueue.h"
#include "core/Log.h"

namespace dfts::player {

PlaybackQueue::~PlaybackQueue() { shutdown(); }

void PlaybackQueue::start(Executor executor) {
    shutdown();
    m_executor = std::move(executor);
    m_running  = true;
    m_thread   = std::thread(&PlaybackQueue::run, this);
}

void PlaybackQueue::shutdown() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_running    = false;
        m_hasPending = false;
    }
    m_cv.notify_all();
    if (m_thread.joinable()) m_thread.join();
}

void PlaybackQueue::submit(Request request) {
    request.generation = ++m_generation;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        // Latest wins: an unstarted request is simply replaced.
        m_pending    = std::move(request);
        m_hasPending = true;
    }
    m_cv.notify_one();
}

void PlaybackQueue::run() {
    for (;;) {
        Request request;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait(lock, [this] { return m_hasPending || !m_running.load(); });
            if (!m_running.load() && !m_hasPending) return;

            request      = std::move(m_pending);
            m_hasPending = false;
        }

        // Drop work the user has already moved past.
        if (request.generation < m_generation.load()) continue;

        m_busy = true;
        try {
            if (m_executor) m_executor(request);
        } catch (const std::exception& e) {
            LOG_E(std::string("playback worker threw: ") + e.what());
        }
        m_busy = false;
    }
}

} // namespace dfts::player
