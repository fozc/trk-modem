@echo off
REM Build and run IEC104 Event Queue test suite on Windows
REM Requires: MinGW GCC or MSYS2 in PATH

echo ================================================
echo IEC104 Event Queue Test Suite - Windows Build
echo ================================================
echo.

REM Check for GCC
where gcc >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo ERROR: GCC not found in PATH
    echo Please install MinGW or MSYS2
    echo Download from: https://www.msys2.org/
    pause
    exit /b 1
)

echo [1/2] Cleaning previous build...
if exist build rmdir /s /q build
mkdir build

echo.
echo [2/2] Compiling simple structure test...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\..\libs ^
    -c iec104_test_simple.c -o build\iec104_test_simple.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile
    pause
    exit /b 1
)

gcc build\iec104_test_simple.o -o build\iec104_test_simple.exe

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Linking failed
    pause
    exit /b 1
)

echo.
echo ================================================
echo Build successful!
echo ================================================
echo.
echo Running tests...
echo.

build\iec104_test_simple.exe

pause


