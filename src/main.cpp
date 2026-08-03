#include "app/App.h"
#include "core/Log.h"

#include <windows.h>
#include <exception>

// Entry point is mainCRTStartup (see CMakeLists) even though the binary is
// linked as /SUBSYSTEM:WINDOWS, so no console window appears.
int main(int, char**) {
    // Per-monitor DPI awareness keeps the UI crisp on scaled displays.
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

    try {
        dfts::App app;
        if (!app.init()) {
            MessageBoxW(nullptr,
                        L"DF Twitch Stream could not start.\r\n"
                        L"See dfts.log in %LOCALAPPDATA%\\DFTwitchStream for details.",
                        L"DF Twitch Stream", MB_ICONERROR | MB_OK);
            return 1;
        }
        int code = app.run();
        app.shutdown();
        return code;
    } catch (const std::exception& e) {
        LOG_E(std::string("fatal: ") + e.what());
        MessageBoxA(nullptr, e.what(), "DF Twitch Stream - fatal error", MB_ICONERROR | MB_OK);
        return 1;
    }
}
