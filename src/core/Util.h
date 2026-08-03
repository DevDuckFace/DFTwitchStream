#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace dfts::util {

// --- encoding ---------------------------------------------------------------
std::wstring widen(const std::string& utf8);
std::string  narrow(const std::wstring& utf16);

// --- string helpers ---------------------------------------------------------
std::string  trim(const std::string& s);
std::string  lower(std::string s);
bool         startsWith(const std::string& s, const std::string& p);
bool         endsWith(const std::string& s, const std::string& p);
bool         icontains(const std::string& hay, const std::string& needle);
std::vector<std::string> split(const std::string& s, char sep);
std::string  join(const std::vector<std::string>& v, const std::string& sep);
std::string  replaceAll(std::string s, const std::string& from, const std::string& to);

// --- url --------------------------------------------------------------------
std::string  urlEncode(const std::string& s);

// --- filesystem -------------------------------------------------------------
std::string  sanitizeFileName(const std::string& s);
std::string  exeDir();
std::string  appDataDir();          // %LOCALAPPDATA%\DFTwitchStream (created)
std::string  defaultDownloadDir();  // %USERPROFILE%\Videos\DFTwitchStream
bool         fileExists(const std::string& path);
bool         ensureDir(const std::string& path);
std::string  findOnPath(const std::string& exeName); // "" if not found

// --- time / format ----------------------------------------------------------
int64_t      unixNow();
// Parses "2024-01-31T20:15:03Z" (RFC3339 / Twitch) into unix seconds. 0 on error.
int64_t      parseIso8601(const std::string& iso);
std::string  formatIso8601(int64_t unixSeconds);        // "YYYY-MM-DDTHH:MM:SSZ"
std::string  formatLocalDateTime(int64_t unixSeconds);  // "31/01/2024 20:15"
// Twitch duration strings such as "3h20m11s" -> seconds.
int          parseTwitchDuration(const std::string& d);
std::string  formatDuration(int seconds);               // "3:20:11"
std::string  formatBytes(uint64_t bytes);
std::string  formatCount(uint64_t n);                   // 12500 -> "12.5K"

// --- misc -------------------------------------------------------------------
std::string  randomHex(size_t bytes);
void         openInBrowser(const std::string& url);
void         setClipboard(const std::string& text);

} // namespace dfts::util
