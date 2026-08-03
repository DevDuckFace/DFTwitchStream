#include "core/Http.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>
#include <winhttp.h>

#include <fstream>
#include <memory>

namespace dfts::http {

namespace {

constexpr const wchar_t* kUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) DFTwitchStream/1.0";

// RAII for the three WinHTTP handle types (they all close with WinHttpCloseHandle).
struct HandleDeleter {
    void operator()(HINTERNET h) const { if (h) WinHttpCloseHandle(h); }
};
using Handle = std::unique_ptr<void, HandleDeleter>;

std::string lastErrorText(const char* what) {
    DWORD e = GetLastError();
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s failed (win32 %lu)", what, (unsigned long)e);
    return buf;
}

Handle openSession(int timeoutMs) {
    Handle session(WinHttpOpen(kUserAgent,
                               WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (session) {
        WinHttpSetTimeouts(session.get(), timeoutMs, timeoutMs, timeoutMs, timeoutMs);
        DWORD proto = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
        WinHttpSetOption(session.get(), WINHTTP_OPTION_SECURE_PROTOCOLS, &proto, sizeof(proto));
        DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
        WinHttpSetOption(session.get(), WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    }
    return session;
}

std::string headerString(HINTERNET request) {
    DWORD size = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                        nullptr, &size, WINHTTP_NO_HEADER_INDEX);
    if (size == 0) return {};
    std::wstring raw(size / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX,
                             raw.data(), &size, WINHTTP_NO_HEADER_INDEX))
        return {};
    return util::narrow(raw);
}

Headers parseHeaders(const std::string& raw) {
    Headers out;
    for (const auto& line : util::split(raw, '\n')) {
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        out[util::lower(util::trim(line.substr(0, colon)))] = util::trim(line.substr(colon + 1));
    }
    return out;
}

// Performs one request without following redirects; caller handles the redirect loop.
struct RawResult {
    int         status = 0;
    std::string error;
    std::string location;
    Headers     headers;
};

// Opens a connection+request pair and sends it. On success `request` holds the
// response ready to be read.
bool performRequest(const Request& req, Handle& session, Handle& connection,
                    Handle& request, RawResult& out) {
    Url u = parseUrl(req.url);
    if (!u.valid) { out.error = "invalid url: " + req.url; return false; }

    session = openSession(req.timeoutMs);
    if (!session) { out.error = lastErrorText("WinHttpOpen"); return false; }

    connection.reset(WinHttpConnect(session.get(), util::widen(u.host).c_str(),
                                    (INTERNET_PORT)u.port, 0));
    if (!connection) { out.error = lastErrorText("WinHttpConnect"); return false; }

    DWORD flags = WINHTTP_FLAG_REFRESH | (u.secure ? WINHTTP_FLAG_SECURE : 0);
    request.reset(WinHttpOpenRequest(connection.get(), util::widen(req.method).c_str(),
                                     util::widen(u.path).c_str(), nullptr,
                                     WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags));
    if (!request) { out.error = lastErrorText("WinHttpOpenRequest"); return false; }

    // We drive redirects ourselves so callers can inspect intermediate hops.
    DWORD policy = WINHTTP_DISABLE_REDIRECTS;
    WinHttpSetOption(request.get(), WINHTTP_OPTION_DISABLE_FEATURE, &policy, sizeof(policy));

    std::string headerBlob;
    for (const auto& [k, v] : req.headers) headerBlob += k + ": " + v + "\r\n";
    std::wstring wHeaders = util::widen(headerBlob);

    BOOL sent = WinHttpSendRequest(
        request.get(),
        wHeaders.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : wHeaders.c_str(),
        wHeaders.empty() ? 0 : (DWORD)-1L,
        req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data(),
        (DWORD)req.body.size(), (DWORD)req.body.size(), 0);
    if (!sent) { out.error = lastErrorText("WinHttpSendRequest"); return false; }

    if (!WinHttpReceiveResponse(request.get(), nullptr)) {
        out.error = lastErrorText("WinHttpReceiveResponse");
        return false;
    }

    DWORD status = 0, len = sizeof(status);
    WinHttpQueryHeaders(request.get(),
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
    out.status  = (int)status;
    out.headers = parseHeaders(headerString(request.get()));
    if (auto it = out.headers.find("location"); it != out.headers.end())
        out.location = it->second;
    return true;
}

bool isRedirect(int status) {
    return status == 301 || status == 302 || status == 303 || status == 307 || status == 308;
}

} // namespace

Url parseUrl(const std::string& url) {
    Url out;
    URL_COMPONENTS c{};
    c.dwStructSize      = sizeof(c);
    c.dwSchemeLength    = (DWORD)-1;
    c.dwHostNameLength  = (DWORD)-1;
    c.dwUrlPathLength   = (DWORD)-1;
    c.dwExtraInfoLength = (DWORD)-1;

    std::wstring w = util::widen(url);
    if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &c)) return out;

    out.scheme = util::narrow(std::wstring(c.lpszScheme, c.dwSchemeLength));
    out.host   = util::narrow(std::wstring(c.lpszHostName, c.dwHostNameLength));
    out.path   = util::narrow(std::wstring(c.lpszUrlPath, c.dwUrlPathLength)) +
                 util::narrow(std::wstring(c.lpszExtraInfo, c.dwExtraInfoLength));
    if (out.path.empty()) out.path = "/";
    out.port   = c.nPort;
    out.secure = (c.nScheme == INTERNET_SCHEME_HTTPS);
    out.valid  = !out.host.empty();
    return out;
}

std::string resolveRelative(const std::string& baseUrl, const std::string& ref) {
    if (ref.empty()) return baseUrl;
    if (util::startsWith(ref, "http://") || util::startsWith(ref, "https://")) return ref;

    Url base = parseUrl(baseUrl);
    if (!base.valid) return ref;
    std::string origin = base.scheme + "://" + base.host;
    if ((base.secure && base.port != 443) || (!base.secure && base.port != 80))
        origin += ":" + std::to_string(base.port);

    if (ref[0] == '/') return origin + ref;

    std::string dir = base.path;
    if (size_t q = dir.find('?'); q != std::string::npos) dir.resize(q);
    size_t slash = dir.find_last_of('/');
    dir = (slash == std::string::npos) ? "/" : dir.substr(0, slash + 1);
    return origin + dir + ref;
}

Response send(const Request& reqIn) {
    Response resp;
    Request  req = reqIn;

    for (int hop = 0; hop <= req.maxRedirect; ++hop) {
        Handle session, connection, request;
        RawResult raw;
        if (!performRequest(req, session, connection, request, raw)) {
            resp.error = raw.error;
            return resp;
        }

        if (isRedirect(raw.status) && !raw.location.empty() && hop < req.maxRedirect) {
            req.url = resolveRelative(req.url, raw.location);
            if (raw.status == 303 || ((raw.status == 301 || raw.status == 302) && req.method == "POST")) {
                req.method = "GET";
                req.body.clear();
            }
            continue;
        }

        resp.status  = raw.status;
        resp.headers = raw.headers;

        DWORD available = 0;
        do {
            available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available)) break;
            if (available == 0) break;
            size_t offset = resp.body.size();
            resp.body.resize(offset + available);
            DWORD read = 0;
            if (!WinHttpReadData(request.get(), resp.body.data() + offset, available, &read)) {
                resp.body.resize(offset);
                break;
            }
            resp.body.resize(offset + read);
        } while (available > 0);

        return resp;
    }

    resp.error = "too many redirects";
    return resp;
}

Response get(const std::string& url, const Headers& h) {
    Request r;
    r.method  = "GET";
    r.url     = url;
    r.headers = h;
    return send(r);
}

Response post(const std::string& url, const std::string& body,
              const Headers& h, const std::string& contentType) {
    Request r;
    r.method  = "POST";
    r.url     = url;
    r.headers = h;
    r.headers["Content-Type"] = contentType;
    r.body    = body;
    return send(r);
}

bool urlExists(const std::string& url, int timeoutMs) {
    Request r;
    r.method    = "GET";   // Some Twitch CDN nodes reject HEAD outright.
    r.url       = url;
    r.timeoutMs = timeoutMs;
    Response resp = send(r);
    return resp.ok();
}

bool download(const std::string& url, const std::string& destPath, const Headers& h,
              const ProgressFn& onProgress, const std::atomic<bool>* cancel) {
    Request req;
    req.method    = "GET";
    req.url       = url;
    req.headers   = h;
    req.timeoutMs = 30000;

    for (int hop = 0; hop <= req.maxRedirect; ++hop) {
        Handle session, connection, request;
        RawResult raw;
        if (!performRequest(req, session, connection, request, raw)) {
            LOG_E("download: " + raw.error);
            return false;
        }
        if (isRedirect(raw.status) && !raw.location.empty() && hop < req.maxRedirect) {
            req.url = resolveRelative(req.url, raw.location);
            continue;
        }
        if (raw.status < 200 || raw.status >= 300) {
            LOG_E("download: HTTP " + std::to_string(raw.status) + " for " + req.url);
            return false;
        }

        uint64_t total = 0;
        if (auto it = raw.headers.find("content-length"); it != raw.headers.end())
            total = std::strtoull(it->second.c_str(), nullptr, 10);

        std::ofstream out(util::widen(destPath), std::ios::binary | std::ios::trunc);
        if (!out) { LOG_E("download: cannot write " + destPath); return false; }

        std::vector<char> buf(64 * 1024);
        uint64_t got = 0;
        for (;;) {
            if (cancel && cancel->load()) return false;
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.get(), &available)) break;
            if (available == 0) break;
            DWORD chunk = (DWORD)std::min<size_t>(buf.size(), available);
            DWORD read  = 0;
            if (!WinHttpReadData(request.get(), buf.data(), chunk, &read) || read == 0) break;
            out.write(buf.data(), read);
            got += read;
            if (onProgress && !onProgress(got, total)) return false;
        }
        out.close();
        return got > 0 || total == 0;
    }
    return false;
}

} // namespace dfts::http
