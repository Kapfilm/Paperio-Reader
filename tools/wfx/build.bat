@echo off
:: Build crosspoint.wfx + crosspoint.wfx64 on Windows using MSYS2.
::
:: Prerequisites (one-time setup):
::   1. Install MSYS2 from https://www.msys2.org/
::   2. Open any MSYS2 shell and run:
::        pacman -S mingw-w64-x86_64-gcc mingw-w64-i686-gcc make zip
::
:: Then just double-click this file (or run it from cmd).
:: Output: dist\crosspoint-usb-wfx-windows.zip

setlocal

:: Locate MSYS2. Adjust this path if you installed MSYS2 elsewhere.
set MSYS2=C:\msys64
if not exist "%MSYS2%\usr\bin\bash.exe" (
    echo ERROR: MSYS2 not found at %MSYS2%
    echo Install from https://www.msys2.org/ then re-run this script.
    pause
    exit /b 1
)

set BASH=%MSYS2%\usr\bin\bash.exe

:: Convert the Windows path of this script to a MSYS2/Unix path.
:: cygpath is available in the MSYS2 base install.
:: Use a temp file to capture cygpath output ? avoids cmd.exe for/f quoting
:: issues with paths that contain a trailing backslash or special characters.
set _WFXWIN=%~dp0
set _WFXWIN=%_WFXWIN:~0,-1%
"%MSYS2%\usr\bin\cygpath.exe" -u "%_WFXWIN%" > "%TEMP%\_wfxdir.tmp"
set /p WFXDIR= < "%TEMP%\_wfxdir.tmp"
del "%TEMP%\_wfxdir.tmp" 2>nul
set WFXDIR=%WFXDIR%/

:: Build 64-bit .wfx64 using the MinGW64 toolchain
echo --- Building crosspoint.wfx64 (64-bit) ---
"%BASH%" --norc -c "PATH=/mingw64/bin:/usr/bin make -C '%WFXDIR%' dist-windows-native NATIVE_OUT=crosspoint.wfx64"
if errorlevel 1 goto fail64

:: Build 32-bit .wfx using the MinGW32 toolchain
echo --- Building crosspoint.wfx (32-bit) ---
"%BASH%" --norc -c "PATH=/mingw32/bin:/usr/bin make -C '%WFXDIR%' dist-windows-native NATIVE_OUT=crosspoint.wfx"
if errorlevel 1 goto fail32

:: Package both into the release zip (PowerShell Compress-Archive; no extra tools needed)
echo --- Packaging ---
copy /y "%~dp0pluginst.inf" "%~dp0dist\win\" >nul
copy /y "%~dp0README.md"    "%~dp0dist\win\" >nul
del /f /q "%~dp0dist\crosspoint-usb-wfx-windows.zip" 2>nul
powershell -NoProfile -Command "Compress-Archive -Path '%~dp0dist\win\crosspoint.wfx','%~dp0dist\win\crosspoint.wfx64','%~dp0dist\win\pluginst.inf','%~dp0dist\win\README.md' -DestinationPath '%~dp0dist\crosspoint-usb-wfx-windows.zip'"
if errorlevel 1 goto failzip

echo.
echo Done: %~dp0dist\crosspoint-usb-wfx-windows.zip
pause
goto :eof

:fail64
echo FAILED (64-bit build). Is mingw-w64-x86_64-gcc installed?
pause
exit /b 1

:fail32
echo FAILED (32-bit build). Is mingw-w64-i686-gcc installed?
pause
exit /b 1

:failzip
echo FAILED (zip). Is zip installed?
pause
exit /b 1
