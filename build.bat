@echo off
setlocal

set "VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
set "NINJA=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"

if not exist "%VCVARS%" (
    echo ERROR: Visual Studio Build Tools not found.
    echo Install "Desktop development with C++" from Visual Studio Installer.
    exit /b 1
)

call "%VCVARS%" >nul
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
    if exist "C:\Program Files\CMake\bin\cmake.exe" (
        set "PATH=C:\Program Files\CMake\bin;%PATH%"
    ) else (
        echo ERROR: cmake not found on PATH.
        exit /b 1
    )
)

if not exist "build\build.ninja" (
    echo Configuring...
    cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_MAKE_PROGRAM="%NINJA%"
    if errorlevel 1 exit /b 1
)

echo Building...
if /i "%~1"=="clean" (
    cmake --build build --clean-first
) else (
    cmake --build build %*
)
if errorlevel 1 exit /b 1

echo.
echo OK: %~dp0build\cs2_overlay.exe
exit /b 0
