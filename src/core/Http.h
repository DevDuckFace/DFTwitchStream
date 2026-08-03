#pragma once
#include <string>
#include <vector>
#include <map>
#include <functional>
#include <atomic>
#include <cstdint>

namespace dfts::http {

struct Response {
    int         status = 0;      // 0 means the request never reached the server
    std::string body;
    std::map<std::string, std::string> headers;
    std::string error;           // populated when status == 0

    bool ok() const { return status >= 200 && status < 300; }
};

using Headers = std::map<std::string, std::string>;

// Called with (bytesSoFar, totalBytesOrZero). Return false to abort the transfer.
using ProgressFn = std::function<bool(uint64_t, uint64_t)>;

struct Request {
    std::string method = "GET";
    std::string url;
    Headers     headers;
    std::string body;
    int         timeoutMs   = 20000;
    int         maxRedirect = 5;
};

// Blocking. Safe to call from any thread; each call owns its WinHTTP handles.
Response send(const Request& req);

Response get (const std::string& url, const Headers& h = {});
Response post(const std::string& url, const std::string& body,
              const Headers& h = {}, const std::string& contentType = "application/json");

// Streams a URL to disk. Returns true on success. `cancel` is polled between chunks.
bool download(const std::string& url, const std::string& destPath,
              const Headers& h = {}, const ProgressFn& onProgress = {},
              const std::atomic<bool>* cancel = nullptr);

// HEAD-style existence probe used by the VOD recovery scanner. Follows redirects.
bool urlExists(const std::string& url, int timeoutMs = 6000);

// A parsed absolute URL.
struct Url {
    std::string scheme, host, path;
    int  port   = 0;
    bool secure = false;
    bool valid  = false;
};
Url parseUrl(const std::string& url);

// Resolves a possibly-relative URI against a base URL (used for HLS playlists).
std::string resolveRelative(const std::string& baseUrl, const std::string& ref);

} // namespace dfts::http
