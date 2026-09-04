@echo off
setlocal
cd /d "%~dp0"

where cmake >nul 2>&1
if errorlevel 1 (
    echo CMake is not on PATH. Install CMake and Visual Studio C++ tools to build.
    exit /b 1
)

set GEN=Visual Studio 17 2022
cmake -S . -B build -G "%GEN%" -A x64
if errorlevel 1 (
    set GEN=Visual Studio 16 2019
    cmake -S . -B build -G "%GEN%" -A x64
    if errorlevel 1 exit /b 1
)

cmake --build build --config Release
if errorlevel 1 exit /b 1
cmake --build build --config Release --target portable
echo.
echo Portable build: dist\PCDataRecovery\PCDataRecovery.exe
endlocal
