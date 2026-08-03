cmake_minimum_required(VERSION 3.21)

project(DFTwitchStream
        VERSION 1.0.0
        DESCRIPTION "Twitch stream browser, player, chat and VOD recovery"
        LANGUAGES C CXX)

if(NOT WIN32)
    message(FATAL_ERROR "DFTwitchStream targets Windows (WinHTTP / Win32 embedding).")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)

option(DFTS_STATIC_RUNTIME "Link the MSVC runtime statically" ON)
if(DFTS_STATIC_RUNTIME AND MSVC)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# ---------------------------------------------------------------------------
# Dependencies
# ---------------------------------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- GLFW -------------------------------------------------------------------
set(GLFW_BUILD_DOCS      OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS     OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES  OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL         OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG        3.4
    GIT_SHALLOW    TRUE)

# --- nlohmann/json ----------------------------------------------------------
set(JSON_BuildTests OFF CACHE INTERNAL "")
FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE)

# --- Dear ImGui -------------------------------------------------------------
# Pinned to a release tag rather than the moving `docking` branch: ImGui 1.92
# replaced the font atlas API (PushFont signature, ImFontAtlas::Build), so an
# unpinned checkout silently breaks the UI code.
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.91.9b-docking
    GIT_SHALLOW    TRUE)

# --- libVLC (video playback, embedded in-process) ---------------------------
# The official win64 build ships the SDK (headers + import libs) alongside the
# runtime DLLs and codec plugins, so one download covers build and runtime.
# libVLC is LGPL: we link against it dynamically and ship it unmodified.
set(DFTS_VLC_VERSION "3.0.21" CACHE STRING "libVLC version to bundle")
# Note: the .7z package is the one that carries sdk/include and sdk/lib; the
# .zip of the same version ships only the portable player.
FetchContent_Declare(vlc
    URL      https://download.videolan.org/pub/videolan/vlc/${DFTS_VLC_VERSION}/win64/vlc-${DFTS_VLC_VERSION}-win64.7z
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)

FetchContent_MakeAvailable(glfw nlohmann_json imgui)
FetchContent_MakeAvailable(vlc)

# Extraction may or may not strip the archive's top-level directory, so locate
# the SDK rather than assuming a fixed layout.
find_path(VLC_INCLUDE_DIR
    NAMES vlc/vlc.h
    PATHS ${vlc_SOURCE_DIR}/sdk/include
          ${vlc_SOURCE_DIR}/vlc-${DFTS_VLC_VERSION}/sdk/include
    NO_DEFAULT_PATH REQUIRED)
find_library(VLC_LIBRARY
    NAMES libvlc vlc
    PATHS ${vlc_SOURCE_DIR}/sdk/lib
          ${vlc_SOURCE_DIR}/vlc-${DFTS_VLC_VERSION}/sdk/lib
    NO_DEFAULT_PATH REQUIRED)
get_filename_component(VLC_SDK_LIB_DIR "${VLC_LIBRARY}" DIRECTORY)
get_filename_component(VLC_RUNTIME_DIR "${VLC_SDK_LIB_DIR}/../.." ABSOLUTE)

if(NOT EXISTS "${VLC_RUNTIME_DIR}/libvlc.dll")
    message(FATAL_ERROR "libvlc.dll not found under ${VLC_RUNTIME_DIR}")
endif()
message(STATUS "libVLC runtime: ${VLC_RUNTIME_DIR}")

add_library(libvlc SHARED IMPORTED)
set_target_properties(libvlc PROPERTIES
    IMPORTED_LOCATION             "${VLC_RUNTIME_DIR}/libvlc.dll"
    IMPORTED_IMPLIB               "${VLC_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${VLC_INCLUDE_DIR}")

add_library(imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC
    ${imgui_SOURCE_DIR}
    ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui PUBLIC glfw)
target_compile_definitions(imgui PUBLIC
    IMGUI_DEFINE_MATH_OPERATORS
    IMGUI_DISABLE_OBSOLETE_FUNCTIONS)

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
set(DFTS_SOURCES
    src/main.cpp
    src/app/App.cpp
    src/app/WindowChrome.cpp
    src/core/Log.cpp
    src/core/Util.cpp
    src/core/Sha1.cpp
    src/core/Http.cpp
    src/core/WebSocket.cpp
    src/core/Config.cpp
    src/core/Process.cpp
    src/i18n/I18n.cpp
    src/twitch/TwitchAuth.cpp
    src/twitch/TwitchApi.cpp
    src/twitch/TwitchGql.cpp
    src/vod/VodRecovery.cpp
    src/vod/Downloader.cpp
    src/player/MpvHost.cpp
    src/player/VlcPlayer.cpp
    src/player/VideoSurface.cpp
    src/player/PlaybackQueue.cpp
    src/chat/ChatClient.cpp
    src/chat/ThirdPartyEmotes.cpp
    src/ui/Theme.cpp
    src/ui/Fonts.cpp
    src/ui/TextureCache.cpp
    src/ui/Cards.cpp
    src/ui/ScreenLogin.cpp
    src/ui/ScreenFollowing.cpp
    src/ui/ScreenBrowse.cpp
    src/ui/ScreenVods.cpp
    src/ui/ScreenRecovery.cpp
    src/ui/ScreenDownloads.cpp
    src/ui/ScreenSettings.cpp
    src/ui/ScreenWatch.cpp)

add_executable(DFTwitchStream WIN32 ${DFTS_SOURCES} res/app.rc)

target_include_directories(DFTwitchStream PRIVATE src)
target_link_libraries(DFTwitchStream PRIVATE
    imgui
    glfw
    nlohmann_json::nlohmann_json
    libvlc
    opengl32
    winhttp
    shell32
    ole32
    oleaut32     # WIC (thumbnail decoding)
    windowscodecs
    shlwapi
    dwmapi
    comctl32
    crypt32)

target_compile_definitions(DFTwitchStream PRIVATE
    UNICODE _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    DFTS_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(DFTwitchStream PRIVATE /W4 /permissive- /utf-8 /MP /wd4100)
    # /MANIFEST:NO because res/app.rc already embeds our own manifest; letting
    # the linker generate a second one is a duplicate-resource error.
    target_link_options(DFTwitchStream PRIVATE /ENTRY:mainCRTStartup /MANIFEST:NO)
endif()

# ---------------------------------------------------------------------------
# Staging / install
# ---------------------------------------------------------------------------
add_custom_command(TARGET DFTwitchStream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/assets $<TARGET_FILE_DIR:DFTwitchStream>/assets)

# libVLC needs its two DLLs next to the exe and its codec plugins in ./plugins.
add_custom_command(TARGET DFTwitchStream POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_RUNTIME_DIR}/libvlc.dll"     $<TARGET_FILE_DIR:DFTwitchStream>
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VLC_RUNTIME_DIR}/libvlccore.dll" $<TARGET_FILE_DIR:DFTwitchStream>
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${VLC_RUNTIME_DIR}/plugins"        $<TARGET_FILE_DIR:DFTwitchStream>/plugins
    COMMENT "Staging libVLC runtime")

install(TARGETS DFTwitchStream RUNTIME DESTINATION .)
install(FILES "${VLC_RUNTIME_DIR}/libvlc.dll" "${VLC_RUNTIME_DIR}/libvlccore.dll"
        DESTINATION .)
install(DIRECTORY "${VLC_RUNTIME_DIR}/plugins/" DESTINATION plugins)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/assets/ DESTINATION assets)
install(FILES ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/LICENSE DESTINATION .)

set(CPACK_PACKAGE_NAME "DFTwitchStream")
set(CPACK_PACKAGE_VENDOR "DF")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
include(CPack)
