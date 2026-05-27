@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "DEPS=%ROOT%.deps"
set "W64_DIR=%DEPS%\w64devkit"
set "RAYLIB_DIR=%DEPS%\raylib"
set "RAYLIB_SRC=%RAYLIB_DIR%\src"
set "DOWNLOADS=%DEPS%\downloads"
set "RAYLIB_EXTRACT=%DEPS%\raylib-5.5-extracted"

set "W64_URL=https://github.com/skeeto/w64devkit/releases/download/v2.8.0/w64devkit-x64-2.8.0.7z.exe"
set "W64_ARCHIVE=%DOWNLOADS%\w64devkit-x64-2.8.0.7z.exe"
set "RAYLIB_URL=https://github.com/raysan5/raylib/releases/download/5.5/raylib-5.5_win64_mingw-w64.zip"
set "RAYLIB_ARCHIVE=%DOWNLOADS%\raylib-5.5_win64_mingw-w64.zip"
set "RAYLIB_PACKAGE=%RAYLIB_EXTRACT%\raylib-5.5_win64_mingw-w64"

if not exist "%DOWNLOADS%" mkdir "%DOWNLOADS%"

if not exist "%W64_DIR%\bin\gcc.exe" (
    echo [setup] Downloading w64devkit...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%W64_URL%' -OutFile '%W64_ARCHIVE%'"
    if errorlevel 1 (
        echo [error] Failed to download w64devkit.
        exit /b 1
    )

    echo [setup] Extracting w64devkit...
    "%W64_ARCHIVE%" -y -o"%DEPS%"
    if errorlevel 1 (
        echo [error] Failed to extract w64devkit.
        exit /b 1
    )
)

if not exist "%RAYLIB_SRC%\raylib.h" (
    echo [setup] Downloading raylib...
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "$ProgressPreference='SilentlyContinue'; Invoke-WebRequest -Uri '%RAYLIB_URL%' -OutFile '%RAYLIB_ARCHIVE%'"
    if errorlevel 1 (
        echo [error] Failed to download raylib.
        exit /b 1
    )

    echo [setup] Extracting raylib...
    if exist "%RAYLIB_EXTRACT%" rmdir /s /q "%RAYLIB_EXTRACT%"
    powershell -NoProfile -ExecutionPolicy Bypass -Command ^
        "Expand-Archive -LiteralPath '%RAYLIB_ARCHIVE%' -DestinationPath '%RAYLIB_EXTRACT%' -Force"
    if errorlevel 1 (
        echo [error] Failed to extract raylib.
        exit /b 1
    )

    if not exist "%RAYLIB_SRC%" mkdir "%RAYLIB_SRC%"
    copy /Y "%RAYLIB_PACKAGE%\include\raylib.h" "%RAYLIB_SRC%\" >nul
    copy /Y "%RAYLIB_PACKAGE%\include\raymath.h" "%RAYLIB_SRC%\" >nul
    copy /Y "%RAYLIB_PACKAGE%\include\rlgl.h" "%RAYLIB_SRC%\" >nul
    copy /Y "%RAYLIB_PACKAGE%\lib\libraylib.a" "%RAYLIB_SRC%\" >nul
    if errorlevel 1 (
        echo [error] Failed to prepare raylib files.
        exit /b 1
    )
)

set "PATH=%W64_DIR%\bin;%PATH%"
set "RAYLIB_PATH=%RAYLIB_DIR%"

echo [setup] Building Gungi...
call "%ROOT%build.bat"
if errorlevel 1 (
    echo [error] Build failed.
    exit /b 1
)

echo [setup] Running rules tests...
"%ROOT%tests\test_rules.exe"
if errorlevel 1 (
    echo [error] Rules tests failed.
    exit /b 1
)

if /I "%~1"=="--no-run" (
    echo [setup] Done. Built "%ROOT%gungi.exe".
    exit /b 0
)

echo [setup] Starting Gungi...
start "" "%ROOT%gungi.exe"
exit /b 0
