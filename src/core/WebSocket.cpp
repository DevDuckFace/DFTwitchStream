#include "core/WebSocket.h"
#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>
#include <winhttp.h>

namespace dfts::net {

namespace {
struct HandleDeleter {
    void operator()(HINTERNET h) const { if (h) WinHttpCloseHandle(h); }
};
using Handle = std::unique_ptr<void, HandleDeleter>;
} // namespace

WebSocket::~WebSocket() { close(); }

void WebSocket::connect(const std::string& wssUrl) {
    close();
    m_running  = true;
    m_thread   = std::thread(&WebSocket::run, this, wssUrl);
}

void WebSocket::abortSocket() {
    std::lock_guard<std::mutex> lock(m_handleMutex);
    if (m_wsHandle) {
        // Closing the handle makes any receive in flight fail immediately;
        // without this the worker sits in WinHttpWebSocketReceive until the
        // timeout expires and close() would block the caller for seconds.
        WinHttpCloseHandle((HINTERNET)m_wsHandle);
        m_wsHandle = nullptr;
    }
}

void WebSocket::close() {
    m_running   = false;
    m_connected = false;
    abortSocket();
    if (m_thread.joinable()) m_thread.join();
    std::lock_guard<std::mutex> lock(m_outMutex);
    m_outbox.clear();
}

void WebSocket::sendText(const std::string& text) {
    std::lock_guard<std::mutex> lock(m_outMutex);
    if (m_outbox.size() > 512) m_outbox.pop_front();   // drop oldest under backpressure
    m_outbox.push_back(text);
}

void WebSocket::flushOutbox(void* wsHandle) {
    for (;;) {
        std::string msg;
        {
            std::lock_guard<std::mutex> lock(m_outMutex);
            if (m_outbox.empty()) return;
            msg = std::move(m_outbox.front());
            m_outbox.pop_front();
        }
        DWORD rc = WinHttpWebSocketSend((HINTERNET)wsHandle,
                                        WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
                                        (PVOID)msg.data(), (DWORD)msg.size());
        if (rc != NO_ERROR) {
            LOG_W("websocket send failed (" + std::to_string(rc) + ")");
            m_connected = false;
            return;
        }
    }
}

void WebSocket::run(std::string url) {
    http::Url u = http::parseUrl(util::replaceAll(util::replaceAll(url, "wss://", "https://"),
                                                  "ws://", "http://"));
    if (!u.valid) {
        if (m_onState) m_onState(false, "invalid websocket url");
        return;
    }

    Handle session(WinHttpOpen(L"DFTwitchStream/1.0",
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) { if (m_onState) m_onState(false, "WinHttpOpen failed"); return; }
    WinHttpSetTimeouts(session.get(), 15000, 15000, 15000, 15000);

    Handle connection(WinHttpConnect(session.get(), util::widen(u.host).c_str(),
                                     (INTERNET_PORT)u.port, 0));
    if (!connection) { if (m_onState) m_onState(false, "WinHttpConnect failed"); return; }

    Handle request(WinHttpOpenRequest(connection.get(), L"GET", util::widen(u.path).c_str(),
                                      nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES,
                                      u.secure ? WINHTTP_FLAG_SECURE : 0));
    if (!request) { if (m_onState) m_onState(false, "WinHttpOpenRequest failed"); return; }

    // Asking for the upgrade before sending is what makes CompleteUpgrade legal below.
    if (!WinHttpSetOption(request.get(), WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0)) {
        if (m_onState) m_onState(false, "upgrade option rejected");
        return;
    }
    if (!WinHttpSendRequest(request.get(), WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            nullptr, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.get(), nullptr)) {
        if (m_onState) m_onState(false, "websocket handshake failed");
        return;
    }

    Handle ws(WinHttpWebSocketCompleteUpgrade(request.get(), 0));
    if (!ws) { if (m_onState) m_onState(false, "websocket upgrade failed"); return; }
    request.reset();   // The socket handle owns the connection from here on.

    // Hand ownership to the shared slot so close() can abort a blocking
    // receive. From here on the handle is only touched under m_handleMutex.
    HINTERNET socket = ws.release();
    {
        std::lock_guard<std::mutex> lock(m_handleMutex);
        if (!m_running.load()) {          // closed while we were connecting
            WinHttpCloseHandle(socket);
            m_connected = false;
            return;
        }
        m_wsHandle = socket;
    }

    m_connected = true;
    if (m_onState) m_onState(true, u.host);

    std::vector<char> buf(8192);
    std::string       assembled;

    while (m_running.load()) {
        flushOutbox(socket);
        if (!m_running.load()) break;

        DWORD read = 0;
        WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
        DWORD rc = WinHttpWebSocketReceive(socket, buf.data(), (DWORD)buf.size(), &read, &type);
        if (rc != NO_ERROR) {
            if (m_running.load()) LOG_W("websocket receive ended (" + std::to_string(rc) + ")");
            break;
        }
        if (type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) break;

        assembled.append(buf.data(), read);
        bool complete = (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                         type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE);
        if (complete) {
            if (m_onMessage) m_onMessage(assembled);
            assembled.clear();
        }
    }

    m_connected = false;
    {
        // close() may already have closed and cleared the handle.
        std::lock_guard<std::mutex> lock(m_handleMutex);
        if (m_wsHandle) {
            WinHttpWebSocketClose((HINTERNET)m_wsHandle,
                                  WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
            WinHttpCloseHandle((HINTERNET)m_wsHandle);
            m_wsHandle = nullptr;
        }
    }
    if (m_onState) m_onState(false, "disconnected");
}

} // namespace dfts::net
