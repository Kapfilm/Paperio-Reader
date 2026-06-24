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
for /f "delims=" %%P in ('"%MSYS2%\usr\bin\cygpath.exe" -u "%~dp0"') do set WFXDIR=%%P

:: Build 64-bit .wfx64 using the MinGW64 toolchain
echo --- Building crosspoint.wfx64 (64-bit) ---
"%BASH%" -lc "PATH=/mingw64/bin:$PATH && make -C '%WFXDIR%' dist-windows-native NATIVE_OUT=crosspoint.wfx64"
if errorlevel 1 ( echo FAILED (64-bit build). Is mingw-w64-x86_64-gcc installed? & pause & exit /b 1 )

:: Build 32-bit .wfx using the MinGW32 toolchain
echo --- Building crosspoint.wfx (32-bit) ---
"%BASH%" -lc "PATH=/mingw32/bin:$PATH && make -C '%WFXDIR%' dist-windows-native NATIVE_OUT=crosspoint.wfx"
if errorlevel 1 ( echo FAILED (32-bit build). Is mingw-w64-i686-gcc installed? & pause & exit /b 1 )

:: Package both into the release zip
echo --- Packaging ---
"%BASH%" -lc "cp '%WFXDIR%pluginst.inf' '%WFXDIR%README.md' '%WFXDIR%dist/win/' && cd '%WFXDIR%dist/win' && zip -j ../crosspoint-usb-wfx-windows.zip crosspoint.wfx crosspoint.wfx64 pluginst.inf README.md"
if errorlevel 1 ( echo FAILED (zip). Is zip installed? & pause & exit /b 1 )

echo.
echo Done: %~dp0dist\crosspoint-usb-wfx-windows.zip
pause
