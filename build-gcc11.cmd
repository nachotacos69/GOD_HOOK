@echo off
setlocal

for %%I in ("%~dp0..\SAMPLE_SCRIPTS\PSPModBase\external\pspsdk") do set "SDKROOT=%%~fI"
set "PSPDEV=%SDKROOT%\usr\local\pspdev"
set "PSPSDK=%PSPDEV%\psp\sdk"
set "PATH=%SDKROOT%\bin;%PSPDEV%\bin;%PATH%"

if not exist "%SDKROOT%\bin\make.exe" (
    echo PSP toolchain not found at "%SDKROOT%"
    exit /b 1
)
where cmake.exe >nul 2>&1
if errorlevel 1 (
    echo CMake is not available on PATH.
    exit /b 1
)

if not defined GEB_ENABLE_LOGGING set "GEB_ENABLE_LOGGING=OFF"
if not defined GEB_BUILD_DIR (
    if /I "%GEB_ENABLE_LOGGING%"=="ON" (
        set "GEB_BUILD_DIR=%~dp0build-debug"
    ) else (
        set "GEB_BUILD_DIR=%~dp0build"
    )
)

cmake -S "%~dp0." -B "%GEB_BUILD_DIR%" -G "Unix Makefiles" "-DCMAKE_MAKE_PROGRAM=%SDKROOT%\bin\make.exe" "-DGEB_ENABLE_LOGGING=%GEB_ENABLE_LOGGING%"
if errorlevel 1 exit /b %ERRORLEVEL%

cmake --build "%GEB_BUILD_DIR%" %*
exit /b %ERRORLEVEL%
