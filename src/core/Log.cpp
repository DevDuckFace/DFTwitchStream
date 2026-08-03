#include "core/Log.h"
#include "core/Util.h"

#include <cstdio>
#include <fstream>

namespace dfts {

Log& Log::get() {
    static Log instance;
    return instance;
}

Log::Log() {
    m_file = util::appDataDir() + "\\dfts.log";
    m_entries.reserve(kMaxEntries);
}

static const char* levelTag(LogLevel l) {
    switch (l) {
        case LogLevel::Debug: return "DBG";
        case LogLevel::Info:  return "INF";
        case LogLevel::Warn:  return "WRN";
        case LogLevel::Error: return "ERR";
    }
    return "???";
}

void Log::write(LogLevel lvl, const std::string& msg) {
    LogEntry e{lvl, util::unixNow(), msg};
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_entries.size() >= kMaxEntries)
            m_entries.erase(m_entries.begin(), m_entries.begin() + kMaxEntries / 4);
        m_entries.push_back(e);
    }
    // Appending per line keeps the log usable if the app is killed mid-download.
    std::ofstream f(util::widen(m_file), std::ios::app);
    if (f) f << util::formatIso8601(e.time) << " [" << levelTag(lvl) << "] " << msg << "\n";
}

std::vector<LogEntry> Log::snapshot() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_entries;
}

void Log::clear() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_entries.clear();
}

} // namespace dfts
