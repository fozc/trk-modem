@echo off
rem Merge the bootloader hex and the application hex into a single
rem flashable Intel HEX file (combined_flash.hex in this repo root).
rem
rem Inputs:
rem   - bootloader hex: Debug build of the troika-smart-breaker-modem-bootlodaer
rem     repo (linked at 0x08000000)
rem   - application hex: Release build of this repo
rem     (STM32U375VETX_BOOT.ld, linked at 0x08014000)
rem
rem Both projects must have "Convert to Intel Hex" enabled in
rem Project Properties - C/C++ Build - Settings.

setlocal
set "ROOT=%~dp0"
set "BOOT=%ROOT%..\troika-smart-breaker-modem-bootlodaer\Debug\troika-smart-breaker-modem-bootloader.hex"
set "APP=%ROOT%Release\troika-smart-breaker-modem.hex"
set "OUT=%ROOT%combined_flash.hex"

if not exist "%BOOT%" (
    echo ERROR: bootloader hex not found:
    echo   %BOOT%
    echo Enable "Convert to Intel Hex" in the bootloader project and build it.
    echo.
    pause
    exit /b 1
)

if not exist "%APP%" (
    echo ERROR: application hex not found:
    echo   %APP%
    echo Build this project in the Release configuration first.
    echo.
    pause
    exit /b 1
)

python "%ROOT%tools\merge_images.py" "%BOOT%" "%APP%" -o "%OUT%"
if errorlevel 1 (
    echo.
    echo Merge FAILED.
) else (
    echo.
    echo Done: %OUT%
)
echo.
pause
