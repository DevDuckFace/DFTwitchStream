@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

rem ===========================================================================
rem  DF Twitch Stream - one-shot build script
rem
rem  Usage:
rem    build.bat                 configure + build (Release)
rem    build.bat debug           build the Debug configuration
rem    build.bat clean           delete the build directory first
rem    build.bat installer       build, then produce the Inno Setup installer
rem    build.bat run             build, then launch the app
rem    build.bat all             clean + build + installer
rem ===========================================================================

set "BUILD_TYPE=Release"
set "DO_CLEAN=0"
set "DO_INSTALLER=0"
set "DO_RUN=0"

:parse_args
if "%~1"=="" goto args_done
if /i "%~1"=="debug"     set "BUILD_TYPE=Debug"
if /i "%~1"=="release"   set "BUILD_TYPE=Release"
if /i "%~1"=="clean"     set "DO_CLEAN=1"
if /i "%~1"=="installer" set "DO_INSTALLER=1"
if /i "%~1"=="run"       set "DO_RUN=1"
if /i "%~1"=="all"       ( set "DO_CLEAN=1" & set "DO_INSTALLER=1" )
shift
goto parse_args
:args_done

set "BUILD_DIR=%~dp0build"
set "DIST_DIR=%~dp0dist"

echo.
echo ==========================================================
echo   DF Twitch Stream - %BUILD_TYPE%
echo ==========================================================
echo.

rem --- locate cmake ---------------------------------------------------------
where cmake >nul 2>&1
if errorlevel 1 (
    echo [ERROR] cmake was not found on PATH.
    echo         Install it from https://cmake.org/download/
    exit /b 1
)

rem --- locate a Visual Studio toolchain -------------------------------------
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "GENERATOR="
rem vswhere reports the toolset major version (16/17/18), not the product year.
if exist "%VSWHERE%" (
    for /f "usebackq tokens=1 delims=." %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationVersion 2^>nul`) do set "VSMAJOR=%%i"
)
if "%VSMAJOR%"=="16" set "GENERATOR=Visual Studio 16 2019"
if "%VSMAJOR%"=="17" set "GENERATOR=Visual Studio 17 2022"
if "%VSMAJOR%"=="18" set "GENERATOR=Visual Studio 18 2026"

if "%GENERATOR%"=="" (
    echo [WARN] Could not identify the Visual Studio version; letting CMake choose.
) else (
    echo [INFO] Generator: %GENERATOR%
)

rem --- clean ----------------------------------------------------------------
if "%DO_CLEAN%"=="1" (
    echo [INFO] Removing %BUILD_DIR%
    if exist "%BUILD_DIR%" rmdir /s /q "%BUILD_DIR%"
    if exist "%DIST_DIR%"  rmdir /s /q "%DIST_DIR%"
)

rem --- configure ------------------------------------------------------------
echo [INFO] Configuring...
if "%GENERATOR%"=="" (
    cmake -S . -B "%BUILD_DIR%" -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
) else (
    cmake -S . -B "%BUILD_DIR%" -G "%GENERATOR%" -A x64 -DCMAKE_BUILD_TYPE=%BUILD_TYPE%
)
if errorlevel 1 (
    echo.
    echo [ERROR] CMake configuration failed.
    exit /b 1
)

rem --- build ----------------------------------------------------------------
echo.
echo [INFO] Building...
cmake --build "%BUILD_DIR%" --config %BUILD_TYPE% --parallel
if errorlevel 1 (
    echo.
    echo [ERROR] Build failed.
    exit /b 1
)

rem --- stage ----------------------------------------------------------------
set "EXE=%BUILD_DIR%\bin\DFTwitchStream.exe"
if not exist "%EXE%" set "EXE=%BUILD_DIR%\bin\%BUILD_TYPE%\DFTwitchStream.exe"
if not exist "%EXE%" (
    echo [ERROR] DFTwitchStream.exe was not produced.
    exit /b 1
)

if not exist "%DIST_DIR%" mkdir "%DIST_DIR%"
copy /y "%EXE%" "%DIST_DIR%\DFTwitchStream.exe" >nul

rem The bundled VLC engine: two DLLs plus its codec plugin tree.
for %%D in (libvlc.dll libvlccore.dll) do (
    if exist "%BUILD_DIR%\bin\%%D" copy /y "%BUILD_DIR%\bin\%%D" "%DIST_DIR%\" >nul
)
if exist "%BUILD_DIR%\bin\plugins" (
    if not exist "%DIST_DIR%\plugins" mkdir "%DIST_DIR%\plugins"
    xcopy /y /e /q "%BUILD_DIR%\bin\plugins\*" "%DIST_DIR%\plugins\" >nul
)
if not exist "%DIST_DIR%\libvlc.dll" (
    echo [ERROR] libvlc.dll is missing from the staged output.
    exit /b 1
)
if exist "%~dp0assets" (
    if not exist "%DIST_DIR%\assets" mkdir "%DIST_DIR%\assets"
    xcopy /y /e /q "%~dp0assets\*" "%DIST_DIR%\assets\" >nul
)
if exist "%~dp0README.md" copy /y "%~dp0README.md" "%DIST_DIR%\" >nul
if exist "%~dp0LICENSE"   copy /y "%~dp0LICENSE"   "%DIST_DIR%\" >nul

echo.
echo [OK] Build finished: %DIST_DIR%\DFTwitchStream.exe

rem --- installer ------------------------------------------------------------
if not "%DO_INSTALLER%"=="1" goto skip_installer

echo.
echo ==========================================================
echo   Installer
echo ==========================================================

rem Locating ISCC happens outside any parenthesised block on purpose: the ")"
rem inside %ProgramFiles(x86)% can close an "if (...)" block early, which makes
rem the detection silently fall through.
set "ISCC="
if exist "%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles(x86)%\Inno Setup 6\ISCC.exe"
if not defined ISCC if exist "%ProgramFiles%\Inno Setup 6\ISCC.exe" set "ISCC=%ProgramFiles%\Inno Setup 6\ISCC.exe"
if not defined ISCC for /f "delims=" %%i in ('where ISCC 2^>nul') do set "ISCC=%%i"

if not defined ISCC (
    echo [ERROR] Inno Setup 6 was not found, so no installer was produced.
    echo         Install it from https://jrsoftware.org/isdl.php
    echo         then run: build.bat installer
    exit /b 1
)
echo [INFO] ISCC: %ISCC%

rem Everything the script references must already be staged, otherwise Inno
rem stops on the first missing file.
if not exist "%DIST_DIR%\DFTwitchStream.exe" (
    echo [ERROR] %DIST_DIR%\DFTwitchStream.exe is missing - build first.
    exit /b 1
)
if not exist "%DIST_DIR%\plugins" (
    echo [ERROR] %DIST_DIR%\plugins is missing - the VLC runtime was not staged.
    exit /b 1
)

rem Delete any previous installer so a stale file cannot be mistaken for a
rem fresh one if this run fails.
del /q "%DIST_DIR%\*setup*.exe" >nul 2>&1

rem No /Q here: when something goes wrong the reason should be on screen.
"%ISCC%" "%~dp0installer\DFTwitchStream.iss"
if errorlevel 1 (
    echo.
    echo [ERROR] Inno Setup returned an error - no installer was produced.
    exit /b 1
)

rem Trust the file on disk, not the exit code.
set "SETUP_FILE="
for %%F in ("%DIST_DIR%\*setup*.exe") do set "SETUP_FILE=%%~fF"
if not defined SETUP_FILE (
    echo.
    echo [ERROR] Inno Setup reported success but produced no file in %DIST_DIR%.
    exit /b 1
)
for %%F in ("%SETUP_FILE%") do (
    echo.
    echo [OK] Installer: %%~fF
    echo      Size: %%~zF bytes
)

:skip_installer
if not "%DO_INSTALLER%"=="1" (
    echo.
    echo [INFO] No installer was requested. Run "build.bat installer" to make one.
)

rem --- run ------------------------------------------------------------------
if "%DO_RUN%"=="1" (
    echo.
    echo [INFO] Launching...
    start "" "%DIST_DIR%\DFTwitchStream.exe"
)

echo.
echo Done.
endlocal
exit /b 0
