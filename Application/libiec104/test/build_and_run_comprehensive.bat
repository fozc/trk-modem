@echo off
REM Build and run IEC104 Event Queue comprehensive test suite on Windows
REM Requires: MinGW GCC or MSYS2 in PATH

echo ========================================================
echo IEC104 Event Queue - Comprehensive Test Suite (Windows)
echo ========================================================
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

echo [1/6] Cleaning previous build...
if exist build rmdir /s /q build
mkdir build

echo.
echo [2/6] Compiling mock_flash.c...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\..\libs -I..\..\bsp ^
    -c mock_flash.c -o build\mock_flash.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile mock_flash.c
    pause
    exit /b 1
)

echo [3/6] Compiling mock_bsp.c...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\..\libs -I..\..\bsp ^
    -c mock_bsp.c -o build\mock_bsp.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile mock_bsp.c
    pause
    exit /b 1
)

echo [4/6] Compiling crc32.c...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\.\libs -I..\..\bsp ^
    -c ..\..\libs\crc32.c -o build\crc32.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile crc.c
    pause
    exit /b 1
)

echo [5/6] Compiling iec104_event_queue.c...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\..\libs -I..\..\bsp ^
    -DTEST_BUILD -c ..\iec104_event_queue.c -o build\iec104_event_queue.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile iec104_event_queue.c
    pause
    exit /b 1
)

echo [6/6] Compiling and linking comprehensive test...
gcc -std=c11 -Wall -Wextra -O2 -g -I.. -I. -I..\..\libs -I..\..\bsp ^
    -c iec104_test_comprehensive.c -o build\iec104_test_comprehensive.o

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Failed to compile comprehensive test
    pause
    exit /b 1
)

gcc build\iec104_test_comprehensive.o build\mock_flash.o build\mock_bsp.o ^
    build\iec104_event_queue.o build\crc32.o ^
    -o build\iec104_test_comprehensive.exe

if %ERRORLEVEL% NEQ 0 (
    echo ERROR: Linking failed
    pause
    exit /b 1
)

echo.
echo ========================================================
echo Build successful!
echo ========================================================
echo.
echo Running comprehensive tests...
echo.

build\iec104_test_comprehensive.exe

echo.
pause
