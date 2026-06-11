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

set "CMAKE="
if exist "C:\Program Files\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
if not defined CMAKE if exist "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not defined CMAKE (
    where cmake >nul 2>&1
    if not errorlevel 1 set "CMAKE=cmake"
)
if not defined CMAKE (
    echo ERROR: cmake not found.
    exit /b 1
)

if not exist "build\build.ninja" (
    echo Configuring...
    "%CMAKE%" -B build -G Ninja -DCMAKE_BUILD_TYPE=Release ^
        -DCMAKE_MAKE_PROGRAM="%NINJA%"
    if errorlevel 1 exit /b 1
)

echo Building...
if /i "%~1"=="clean" (
    "%CMAKE%" --build build --clean-first
) else (
    "%CMAKE%" --build build %*
)
if errorlevel 1 exit /b 1

echo.
echo OK: %~dp0build\cs2_overlay.exe
exit /b 0
