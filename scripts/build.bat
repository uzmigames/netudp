@echo off
setlocal

:: netudp build script (Windows)
:: Usage:
::   scripts\build.bat              debug build + tests
::   scripts\build.bat release      release build + tests
::   scripts\build.bat clean        remove all build dirs
::   scripts\build.bat test         run tests only
::   scripts\build.bat cross-linux  cross-compile for Linux via Zig CC
::   scripts\build.bat msvc         build with MSVC (no Zig)
::   scripts\build.bat all          debug + release + tests

set ROOT=%~dp0..
cd /d "%ROOT%"

if "%1"=="" set PRESET=debug& goto :debug
set PRESET=%1

if "%PRESET%"=="debug" goto :debug
if "%PRESET%"=="release" goto :release
if "%PRESET%"=="test" goto :test
if "%PRESET%"=="bench" goto :bench
if "%PRESET%"=="cross-linux" goto :cross-linux
if "%PRESET%"=="msvc" goto :msvc
if "%PRESET%"=="clean" goto :clean
if "%PRESET%"=="all" goto :all

echo Usage: %~nx0 {debug^|release^|bench^|test^|cross-linux^|msvc^|clean^|all}
exit /b 1

:debug
echo === Configure (debug, Zig CC) ===
cmake --preset debug
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build build-debug -j 4
if errorlevel 1 exit /b 1
echo === Test ===
ctest --test-dir build-debug --output-on-failure
if errorlevel 1 exit /b 1
echo === Done: build-debug\libnetudp.a ===
goto :eof

:release
echo === Configure (release, Zig CC) ===
cmake --preset release
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build build-release -j 4
if errorlevel 1 exit /b 1
echo === Test ===
ctest --test-dir build-release --output-on-failure
if errorlevel 1 exit /b 1
echo === Done: build-release\libnetudp.a ===
goto :eof

:bench
echo === Configure (release + bench, Zig CC) ===
cmake --preset release
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build build-release -j 4
if errorlevel 1 exit /b 1
echo === Benchmarks ===
if exist build-release\bench\bench_pps.exe (
    build-release\bench\bench_pps.exe
) else (
    echo No benchmarks found (bench/ targets not yet implemented)
)
goto :eof

:test
if exist build-debug (
    echo === Test (debug) ===
    ctest --test-dir build-debug --output-on-failure
) else if exist build-release (
    echo === Test (release) ===
    ctest --test-dir build-release --output-on-failure
) else (
    echo No build directory found. Run: scripts\build.bat debug
    exit /b 1
)
goto :eof

:cross-linux
echo === Configure (cross-linux, Zig CC) ===
cmake --preset cross-linux
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build build-linux -j 4
if errorlevel 1 exit /b 1
echo === Done: build-linux\libnetudp.a (ELF) ===
goto :eof

:msvc
echo === Configure (MSVC, no Zig) ===
cmake --preset msvc
if errorlevel 1 exit /b 1
echo === Build ===
cmake --build build-msvc --config Debug -j 4
if errorlevel 1 exit /b 1
echo === Test ===
ctest --test-dir build-msvc -C Debug --output-on-failure
if errorlevel 1 exit /b 1
echo === Done: build-msvc\Debug\netudp.lib ===
goto :eof

:clean
echo === Cleaning all build directories ===
if exist build-debug rmdir /s /q build-debug
if exist build-release rmdir /s /q build-release
if exist build-reldbg rmdir /s /q build-reldbg
if exist build-linux rmdir /s /q build-linux
if exist build-msvc rmdir /s /q build-msvc
if exist build rmdir /s /q build
echo === Done ===
goto :eof

:all
call "%~f0" debug
if errorlevel 1 exit /b 1
call "%~f0" release
if errorlevel 1 exit /b 1
goto :eof
