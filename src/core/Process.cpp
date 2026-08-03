#include "core/Process.h"
#include "core/Util.h"
#include "core/Log.h"

#include <windows.h>
#include <utility>

namespace dfts::proc {

std::string quoteArg(const std::string& arg) {
    if (!arg.empty() && arg.find_first_of(" \t\"") == std::string::npos) return arg;

    std::string out = "\"";
    size_t backslashes = 0;
    for (char c : arg) {
        if (c == '\\') {
            ++backslashes;
        } else if (c == '"') {
            out.append(backslashes * 2 + 1, '\\');
            out += '"';
            backslashes = 0;
        } else {
            out.append(backslashes, '\\');
            out += c;
            backslashes = 0;
        }
    }
    out.append(backslashes * 2, '\\');
    out += '"';
    return out;
}

static std::string buildCommandLine(const std::string& exe, const std::vector<std::string>& args) {
    std::string cmd = quoteArg(exe);
    for (const auto& a : args) cmd += " " + quoteArg(a);
    return cmd;
}

Child::~Child() { kill(); }

Child::Child(Child&& other) noexcept
    : m_process(std::exchange(other.m_process, nullptr)),
      m_thread(std::exchange(other.m_thread, nullptr)),
      m_pid(std::exchange(other.m_pid, 0)) {}

Child& Child::operator=(Child&& other) noexcept {
    if (this != &other) {
        kill();
        m_process = std::exchange(other.m_process, nullptr);
        m_thread  = std::exchange(other.m_thread, nullptr);
        m_pid     = std::exchange(other.m_pid, 0);
    }
    return *this;
}

bool Child::start(const std::string& exePath, const std::vector<std::string>& args, bool hideWindow) {
    kill();

    std::wstring cmd = util::widen(buildCommandLine(exePath, args));
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (hideWindow) {
        si.dwFlags     = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;
    }
    PROCESS_INFORMATION pi{};

    DWORD flags = CREATE_NO_WINDOW;
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        flags, nullptr, nullptr, &si, &pi)) {
        LOG_E("cannot start " + exePath + " (win32 " + std::to_string(GetLastError()) + ")");
        return false;
    }
    m_process = pi.hProcess;
    m_thread  = pi.hThread;
    m_pid     = pi.dwProcessId;
    return true;
}

bool Child::running() const {
    if (!m_process) return false;
    return WaitForSingleObject((HANDLE)m_process, 0) == WAIT_TIMEOUT;
}

void Child::kill() {
    if (m_process) {
        if (running()) TerminateProcess((HANDLE)m_process, 0);
        CloseHandle((HANDLE)m_process);
        m_process = nullptr;
    }
    if (m_thread) {
        CloseHandle((HANDLE)m_thread);
        m_thread = nullptr;
    }
    m_pid = 0;
}

void Child::detach() {
    if (m_process) { CloseHandle((HANDLE)m_process); m_process = nullptr; }
    if (m_thread)  { CloseHandle((HANDLE)m_thread);  m_thread  = nullptr; }
    m_pid = 0;
}

int runCapture(const std::string& exePath, const std::vector<std::string>& args,
               std::string& output, int timeoutMs) {
    output.clear();

    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE readPipe = nullptr, writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) return -1;
    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    std::wstring cmd = util::widen(buildCommandLine(exePath, args));
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb         = sizeof(si);
    si.dwFlags    = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = writePipe;
    si.hStdError  = writePipe;
    si.hStdInput  = nullptr;
    PROCESS_INFORMATION pi{};

    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(readPipe);
        CloseHandle(writePipe);
        return -1;
    }
    CloseHandle(writePipe);   // Only the child keeps the write end alive.

    char buf[4096];
    DWORD read = 0;
    while (ReadFile(readPipe, buf, sizeof(buf), &read, nullptr) && read > 0)
        output.append(buf, read);
    CloseHandle(readPipe);

    DWORD exitCode = (DWORD)-1;
    if (WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs) == WAIT_TIMEOUT)
        TerminateProcess(pi.hProcess, 1);
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)exitCode;
}

} // namespace dfts::proc
