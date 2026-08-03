#pragma once
#include <string>
#include <vector>
#include <mutex>

namespace dfts {

enum class LogLevel { Debug, Info, Warn, Error };

struct LogEntry {
    LogLevel    level;
    int64_t     time;
    std::string text;
};

// Thread-safe ring buffer that also mirrors to %LOCALAPPDATA%\DFTwitchStream\dfts.log.
class Log {
public:
    static Log& get();

    void write(LogLevel lvl, const std::string& msg);
    void debug(const std::string& m) { write(LogLevel::Debug, m); }
    void info (const std::string& m) { write(LogLevel::Info,  m); }
    void warn (const std::string& m) { write(LogLevel::Warn,  m); }
    void error(const std::string& m) { write(LogLevel::Error, m); }

    std::vector<LogEntry> snapshot() const;
    void clear();
    std::string filePath() const { return m_file; }

private:
    Log();
    mutable std::mutex    m_mutex;
    std::vector<LogEntry> m_entries;
    std::string           m_file;
    static constexpr size_t kMaxEntries = 2000;
};

} // namespace dfts

#define LOG_D(msg) ::dfts::Log::get().debug(msg)
#define LOG_I(msg) ::dfts::Log::get().info(msg)
#define LOG_W(msg) ::dfts::Log::get().warn(msg)
#define LOG_E(msg) ::dfts::Log::get().error(msg)
