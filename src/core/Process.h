#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace dfts::proc {

// Owns a spawned child process. Killing on destruction keeps mpv from outliving
// the app when the user closes the window mid-playback.
class Child {
public:
    Child() = default;
    ~Child();

    Child(const Child&)            = delete;
    Child& operator=(const Child&) = delete;
    Child(Child&& other) noexcept;
    Child& operator=(Child&& other) noexcept;

    // `args` are passed verbatim; each element is quoted as needed.
    bool start(const std::string& exePath, const std::vector<std::string>& args,
               bool hideWindow = true);

    bool running() const;
    void kill();
    void detach();          // stop owning the handle (process keeps running)
    uint32_t pid() const { return m_pid; }

private:
    void* m_process = nullptr;   // HANDLE
    void* m_thread  = nullptr;   // HANDLE
    uint32_t m_pid  = 0;
};

// Runs a process to completion and captures stdout+stderr. Returns exit code,
// or -1 if the process could not be started.
int runCapture(const std::string& exePath, const std::vector<std::string>& args,
               std::string& output, int timeoutMs = 120000);

// Quotes one argument following the CommandLineToArgvW rules.
std::string quoteArg(const std::string& arg);

} // namespace dfts::proc
