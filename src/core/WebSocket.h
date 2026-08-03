#pragma once
#include <string>
#include <functional>
#include <atomic>
#include <thread>
#include <mutex>
#include <deque>

namespace dfts::net {

// Minimal text WebSocket client on top of WinHTTP's native upgrade support.
// One background thread owns the socket; callbacks fire on that thread.
class WebSocket {
public:
    using MessageFn = std::function<void(const std::string&)>;
    using StateFn   = std::function<void(bool connected, const std::string& info)>;

    WebSocket() = default;
    ~WebSocket();

    WebSocket(const WebSocket&)            = delete;
    WebSocket& operator=(const WebSocket&) = delete;

    void onMessage(MessageFn fn) { m_onMessage = std::move(fn); }
    void onState(StateFn fn)     { m_onState   = std::move(fn); }

    // Connects asynchronously. `wssUrl` must be wss:// or ws://.
    void connect(const std::string& wssUrl);
    void close();

    bool connected() const { return m_connected.load(); }

    // Queues a UTF-8 text frame. Safe from any thread; a no-op while disconnected.
    void sendText(const std::string& text);

private:
    void run(std::string url);
    void flushOutbox(void* wsHandle);
    // Closes the socket handle so a blocking receive returns at once.
    void abortSocket();

    std::thread             m_thread;
    std::atomic<bool>       m_running{false};
    std::atomic<bool>       m_connected{false};
    std::mutex              m_outMutex;
    std::deque<std::string> m_outbox;
    MessageFn               m_onMessage;
    StateFn                 m_onState;

    // Guards m_wsHandle so the caller and the worker never close it twice.
    std::mutex              m_handleMutex;
    void*                   m_wsHandle = nullptr;
};

} // namespace dfts::net
