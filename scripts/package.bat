@echo off
setlocal enabledelayedexpansion

:: netudp Package Script — builds DLL + headers + SDK into dist/
:: Usage: scripts\package.bat [release|debug]

set "SCRIPT_DIR=%~dp0"
set "ROOT_DIR=%SCRIPT_DIR%.."
cd /d "%ROOT_DIR%"
set "ROOT_DIR=%CD%"

set "BUILD_TYPE=release"
if /i "%~1"=="debug" set "BUILD_TYPE=debug"

set "BUILD_DIR=%ROOT_DIR%\build\%BUILD_TYPE%"
set "DIST_DIR=%ROOT_DIR%\dist\netudp"

echo.
echo ========================================
echo     netudp Package Builder
echo ========================================
echo.
echo Build type:  %BUILD_TYPE%
echo Output:      %DIST_DIR%
echo.

:: Step 1: Build static + shared
echo [1/4] Building static + shared library...
set "CACHE_DIR=%BUILD_DIR%"
if not exist "%CACHE_DIR%" mkdir "%CACHE_DIR%"

:: Auto-detect zig
set "TOOLCHAIN_ARGS="
set "CMAKE_GEN=-G Ninja"
where zig.exe >nul 2>&1
if !errorlevel!==0 (
    set "TOOLCHAIN_ARGS=-DCMAKE_TOOLCHAIN_FILE=%ROOT_DIR%\cmake\zig-toolchain.cmake"
    echo Compiler: Zig CC
) else (
    where cl.exe >nul 2>&1
    if !errorlevel!==0 (
        set "CMAKE_GEN=-G Ninja -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl"
        echo Compiler: MSVC
    ) else (
        echo Compiler: Default
        set "CMAKE_GEN="
    )
)

set "CMAKE_BUILD_TYPE=Release"
if /i "%BUILD_TYPE%"=="debug" set "CMAKE_BUILD_TYPE=Debug"

cd /d "%CACHE_DIR%"
cmake "%ROOT_DIR%" ^
    %CMAKE_GEN% ^
    %TOOLCHAIN_ARGS% ^
    -DCMAKE_BUILD_TYPE=%CMAKE_BUILD_TYPE% ^
    -DNETUDP_BUILD_TESTS=OFF ^
    -DNETUDP_BUILD_SHARED=ON ^
    -DNETUDP_DISABLE_PROFILING=ON

if errorlevel 1 (
    echo CMake configure failed!
    exit /b 1
)

cmake --build . --config %CMAKE_BUILD_TYPE% --parallel
if errorlevel 1 (
    echo Build failed!
    exit /b 1
)

cd /d "%ROOT_DIR%"

:: Step 2: Create dist layout
echo [2/4] Creating distribution layout...
if exist "%DIST_DIR%" rmdir /s /q "%DIST_DIR%"
mkdir "%DIST_DIR%"
mkdir "%DIST_DIR%\include\netudp"
mkdir "%DIST_DIR%\lib\win-x64"
mkdir "%DIST_DIR%\sdk\cpp"

:: Step 3: Copy files
echo [3/4] Copying files...

:: Headers
copy /y "include\netudp\netudp.h"          "%DIST_DIR%\include\netudp\" >nul
copy /y "include\netudp\netudp_types.h"    "%DIST_DIR%\include\netudp\" >nul
copy /y "include\netudp\netudp_config.h"   "%DIST_DIR%\include\netudp\" >nul
copy /y "include\netudp\netudp_buffer.h"   "%DIST_DIR%\include\netudp\" >nul
copy /y "include\netudp\netudp_token.h"    "%DIST_DIR%\include\netudp\" >nul
copy /y "include\netudp\netudp_profiling.h" "%DIST_DIR%\include\netudp\" >nul

:: Static lib
if exist "%BUILD_DIR%\libnetudp.a" (
    copy /y "%BUILD_DIR%\libnetudp.a" "%DIST_DIR%\lib\win-x64\" >nul
)
if exist "%BUILD_DIR%\netudp.lib" (
    copy /y "%BUILD_DIR%\netudp.lib" "%DIST_DIR%\lib\win-x64\" >nul
)
if exist "%BUILD_DIR%\%CMAKE_BUILD_TYPE%\netudp.lib" (
    copy /y "%BUILD_DIR%\%CMAKE_BUILD_TYPE%\netudp.lib" "%DIST_DIR%\lib\win-x64\" >nul
)

:: Shared lib (DLL)
if exist "%BUILD_DIR%\netudp.dll" (
    copy /y "%BUILD_DIR%\netudp.dll" "%DIST_DIR%\lib\win-x64\" >nul
    echo   Found: netudp.dll
)
if exist "%BUILD_DIR%\libnetudp.dll" (
    copy /y "%BUILD_DIR%\libnetudp.dll" "%DIST_DIR%\lib\win-x64\netudp.dll" >nul
    echo   Found: libnetudp.dll
)
if exist "%BUILD_DIR%\netudp.dll.a" (
    copy /y "%BUILD_DIR%\netudp.dll.a" "%DIST_DIR%\lib\win-x64\" >nul
)
if exist "%BUILD_DIR%\libnetudp.dll.a" (
    copy /y "%BUILD_DIR%\libnetudp.dll.a" "%DIST_DIR%\lib\win-x64\netudp.dll.a" >nul
)

:: C++ SDK
copy /y "sdk\cpp\netudp.hpp" "%DIST_DIR%\sdk\cpp\" >nul
copy /y "sdk\cpp\README.md"  "%DIST_DIR%\sdk\cpp\" >nul
copy /y "sdk\cpp\example.cpp" "%DIST_DIR%\sdk\cpp\" >nul

:: License
if exist "LICENSE" copy /y "LICENSE" "%DIST_DIR%\" >nul

:: Step 4: Print result
echo [4/4] Done!
echo.
echo ========================================
echo    Distribution: %DIST_DIR%
echo ========================================
echo.
dir /s /b "%DIST_DIR%" 2>nul | find /c /v "" >nul
echo.
echo Layout:
echo   include\netudp\    Public C headers (6 files)
echo   lib\win-x64\       Static lib + DLL
echo   sdk\cpp\            C++ SDK header + example
echo   LICENSE
echo.
echo Usage (consumer):
echo   zig c++ -std=c++17 -Iinclude -Isdk/cpp app.cpp -Llib/win-x64 -lnetudp -o app
echo.

endlocal
