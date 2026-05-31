@echo off
setlocal

set "ROOT=%~dp0"
if defined RAYLIB_PATH (
    set "RAYLIB_DIR=%RAYLIB_PATH%"
) else (
    set "RAYLIB_DIR=C:\raylib\raylib"
)

set "GCC=gcc"
where gcc >nul 2>nul
if errorlevel 1 (
    if exist "%RAYLIB_DIR%\..\w64devkit\bin\gcc.exe" (
        set "GCC=%RAYLIB_DIR%\..\w64devkit\bin\gcc.exe"
    ) else (
        echo [error] gcc was not found.
        echo         Install the raylib Windows package and add C:\raylib\w64devkit\bin to PATH,
        echo         or install another MinGW-w64 gcc and make sure gcc is available.
        exit /b 1
    )
)

if not exist "%RAYLIB_DIR%\src\raylib.h" (
    echo [error] raylib.h was not found at "%RAYLIB_DIR%\src".
    echo         Set RAYLIB_PATH to the raylib folder, for example:
    echo         set RAYLIB_PATH=C:\raylib\raylib
    exit /b 1
)

if not exist "%RAYLIB_DIR%\src\libraylib.a" (
    echo [error] libraylib.a was not found at "%RAYLIB_DIR%\src".
    echo         Build or reinstall raylib for Windows before running this script.
    exit /b 1
)

if not exist "%ROOT%src\main.c" (
    echo [error] Missing "%ROOT%src\main.c".
    exit /b 1
)

if not exist "%ROOT%src\gungi_rules.c" (
    echo [error] Missing "%ROOT%src\gungi_rules.c".
    echo         The UI slice expects the rules engine file to be provided by the rules slice.
    exit /b 1
)

if not exist "%ROOT%tests\test_rules.c" (
    echo [error] Missing "%ROOT%tests\test_rules.c".
    echo         The rules test source is required to build tests\test_rules.exe.
    exit /b 1
)

if not exist "%ROOT%tests" mkdir "%ROOT%tests"
if not exist "%ROOT%tools" mkdir "%ROOT%tools"

echo Building gungi.exe...
"%GCC%" -O3 -fopenmp -std=c99 -Wall -Wextra -pedantic -I"%RAYLIB_DIR%\src" -I"%ROOT%src" "%ROOT%src\ai_core.c" "%ROOT%src\q_model.c" "%ROOT%src\main.c" "%ROOT%src\gungi_rules.c" -o "%ROOT%gungi.exe" -L"%RAYLIB_DIR%\src" -lraylib -lopengl32 -lgdi32 -lwinmm -lm -fopenmp
if errorlevel 1 (
    echo [error] Failed to build gungi.exe.
    exit /b 1
)

echo Building tests\test_rules.exe...
"%GCC%" -O3 -fopenmp -std=c99 -Wall -Wextra -pedantic -I"%ROOT%src" "%ROOT%tests\test_rules.c" "%ROOT%src\ai_core.c" "%ROOT%src\gungi_rules.c" -o "%ROOT%tests\test_rules.exe" -lm -fopenmp
if errorlevel 1 (
    echo [error] Failed to build tests\test_rules.exe.
    exit /b 1
)

echo Building tools\train_q.exe...
"%GCC%" -O3 -fopenmp -std=c99 -Wall -Wextra -pedantic -I"%ROOT%src" "%ROOT%tools\train_q.c" "%ROOT%src\q_model.c" "%ROOT%src\ai_core.c" "%ROOT%src\gungi_rules.c" -o "%ROOT%tools\train_q.exe" -lm -fopenmp
if errorlevel 1 (
    echo [error] Failed to build tools\train_q.exe.
    exit /b 1
)

echo Build completed:
echo   %ROOT%gungi.exe
echo   %ROOT%tests\test_rules.exe
echo   %ROOT%tools\train_q.exe
exit /b 0
