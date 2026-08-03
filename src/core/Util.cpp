#include "core/Util.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <wincrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

namespace dfts::util {

std::wstring widen(const std::string& utf8) {
    if (utf8.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), nullptr, 0);
    std::wstring out((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), (int)utf8.size(), out.data(), n);
    return out;
}

std::string narrow(const std::wstring& utf16) {
    if (utf16.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), nullptr, 0, nullptr, nullptr);
    std::string out((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, utf16.data(), (int)utf16.size(), out.data(), n, nullptr, nullptr);
    return out;
}

std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

bool startsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(0, p.size(), p) == 0;
}

bool endsWith(const std::string& s, const std::string& p) {
    return s.size() >= p.size() && s.compare(s.size() - p.size(), p.size(), p) == 0;
}

bool icontains(const std::string& hay, const std::string& needle) {
    if (needle.empty()) return true;
    return lower(hay).find(lower(needle)) != std::string::npos;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    std::istringstream ss(s);
    while (std::getline(ss, cur, sep)) out.push_back(cur);
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) out += sep;
        out += v[i];
    }
    return out;
}

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    if (from.empty()) return s;
    size_t pos = 0;
    while ((pos = s.find(from, pos)) != std::string::npos) {
        s.replace(pos, from.size(), to);
        pos += to.size();
    }
    return s;
}

std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 0x0F];
        }
    }
    return out;
}

std::string sanitizeFileName(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        if (strchr("\\/:*?\"<>|", c) || (unsigned char)c < 0x20) out += '_';
        else out += c;
    }
    out = trim(out);
    while (!out.empty() && (out.back() == '.' || out.back() == ' ')) out.pop_back();
    if (out.empty()) out = "untitled";
    if (out.size() > 150) out.resize(150);
    return out;
}

std::string exeDir() {
    wchar_t buf[MAX_PATH]{};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return narrow(fs::path(buf).parent_path().wstring());
}

static std::string knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p = nullptr;
    std::string out;
    if (SUCCEEDED(SHGetKnownFolderPath(id, 0, nullptr, &p))) {
        out = narrow(p);
        CoTaskMemFree(p);
    }
    return out;
}

std::string appDataDir() {
    std::string base = knownFolder(FOLDERID_LocalAppData);
    if (base.empty()) base = exeDir();
    std::string dir = base + "\\DFTwitchStream";
    ensureDir(dir);
    return dir;
}

std::string defaultDownloadDir() {
    std::string base = knownFolder(FOLDERID_Videos);
    if (base.empty()) base = knownFolder(FOLDERID_Profile);
    if (base.empty()) base = exeDir();
    return base + "\\DFTwitchStream";
}

bool fileExists(const std::string& path) {
    std::error_code ec;
    return fs::exists(fs::path(widen(path)), ec);
}

bool ensureDir(const std::string& path) {
    std::error_code ec;
    fs::path p(widen(path));
    if (fs::exists(p, ec)) return fs::is_directory(p, ec);
    return fs::create_directories(p, ec);
}

std::string findOnPath(const std::string& exeName) {
    std::wstring name = widen(exeName);
    wchar_t buf[MAX_PATH * 4]{};
    wchar_t* filePart = nullptr;
    DWORD n = SearchPathW(nullptr, name.c_str(), L".exe", _countof(buf), buf, &filePart);
    if (n > 0 && n < _countof(buf)) return narrow(buf);

    // Common install locations that are frequently missing from PATH.
    static const wchar_t* roots[] = {
        L"C:\\Program Files\\", L"C:\\Program Files (x86)\\",
        L"C:\\ProgramData\\chocolatey\\bin\\", L"C:\\tools\\",
    };
    for (const wchar_t* root : roots) {
        std::wstring direct = std::wstring(root) + name + L".exe";
        std::error_code ec;
        if (fs::exists(direct, ec)) return narrow(direct);
        std::wstring nested = std::wstring(root) + name + L"\\" + name + L".exe";
        if (fs::exists(nested, ec)) return narrow(nested);
    }
    return {};
}

int64_t unixNow() { return (int64_t)std::time(nullptr); }

int64_t parseIso8601(const std::string& iso) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 3) return 0;
    if (y < 1970) return 0;

    // Days since epoch (civil-from-days algorithm, UTC — no timezone lookups).
    int yy = y - (mo <= 2 ? 1 : 0);
    int era = (yy >= 0 ? yy : yy - 399) / 400;
    unsigned yoe = (unsigned)(yy - era * 400);
    unsigned doy = (unsigned)((153 * (mo + (mo > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    return days * 86400 + h * 3600 + mi * 60 + s;
}

std::string formatIso8601(int64_t unixSeconds) {
    std::time_t t = (std::time_t)unixSeconds;
    std::tm tm{};
    gmtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buf;
}

std::string formatLocalDateTime(int64_t unixSeconds) {
    if (unixSeconds <= 0) return "-";
    std::time_t t = (std::time_t)unixSeconds;
    std::tm tm{};
    localtime_s(&tm, &t);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%d/%m/%Y %H:%M", &tm);
    return buf;
}

int parseTwitchDuration(const std::string& d) {
    int total = 0, cur = 0;
    for (char c : d) {
        if (std::isdigit((unsigned char)c)) {
            cur = cur * 10 + (c - '0');
        } else {
            if (c == 'h') total += cur * 3600;
            else if (c == 'm') total += cur * 60;
            else if (c == 's') total += cur;
            cur = 0;
        }
    }
    return total;
}

std::string formatDuration(int seconds) {
    if (seconds < 0) seconds = 0;
    int h = seconds / 3600, m = (seconds % 3600) / 60, s = seconds % 60;
    char buf[32];
    if (h > 0) std::snprintf(buf, sizeof(buf), "%d:%02d:%02d", h, m, s);
    else       std::snprintf(buf, sizeof(buf), "%d:%02d", m, s);
    return buf;
}

std::string formatBytes(uint64_t bytes) {
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; ++u; }
    char buf[48];
    std::snprintf(buf, sizeof(buf), u == 0 ? "%.0f %s" : "%.1f %s", v, units[u]);
    return buf;
}

std::string formatCount(uint64_t n) {
    char buf[32];
    if (n >= 1000000) std::snprintf(buf, sizeof(buf), "%.1fM", n / 1000000.0);
    else if (n >= 1000) std::snprintf(buf, sizeof(buf), "%.1fK", n / 1000.0);
    else std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)n);
    return buf;
}

std::string randomHex(size_t bytes) {
    std::vector<unsigned char> buf(bytes);
    HCRYPTPROV prov = 0;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(prov, (DWORD)bytes, buf.data());
        CryptReleaseContext(prov, 0);
    }
    static const char* hex = "0123456789abcdef";
    std::string out;
    for (unsigned char c : buf) { out += hex[c >> 4]; out += hex[c & 0xF]; }
    return out;
}

void openInBrowser(const std::string& url) {
    ShellExecuteW(nullptr, L"open", widen(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

void setClipboard(const std::string& text) {
    if (!OpenClipboard(nullptr)) return;
    EmptyClipboard();
    std::wstring w = widen(text);
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (h) {
        void* p = GlobalLock(h);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

} // namespace dfts::util
