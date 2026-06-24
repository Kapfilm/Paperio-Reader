# Build / package the CrossPoint USB WFX plugin for Total / Double Commander.
#
#   make                      # native Linux build -> crosspoint.wfx
#   make dist-linux           # release zip: crosspoint-usb-wfx-linux-x86_64.zip
#   make dist-windows         # release zip: crosspoint-usb-wfx-windows.zip
#                               (Linux/WSL only: needs i686-/x86_64-w64-mingw32-gcc)
#   make dist-windows-native  # same zip, from an MSYS2 MinGW64 shell on Windows
#                               (run once from MinGW64 shell, once from MinGW32 shell,
#                               or just run build.bat which does both automatically)
#   make clean
#
# A release zip contains the plugin binary + pluginst.inf so Total/Double
# Commander can install it with one click (open the zip from within the
# commander). The .wfx is a native binary, so each OS gets its own zip.
#
# On Windows: use build.bat (it calls MSYS2 make for both 32-bit and 64-bit).
# Install MSYS2 from https://www.msys2.org/, then in any MSYS2 shell:
#   pacman -S mingw-w64-x86_64-gcc mingw-w64-i686-gcc make zip

SRC    := crosspoint.c cp_serial.c
HDR    := cp_serial.h wfxplugin.h
EXTRA  := pluginst.inf README.md
CFLAGS := -O2 -Wall -Wextra -Wno-format-truncation -fvisibility=hidden
WINCC32 := i686-w64-mingw32-gcc
WINCC64 := x86_64-w64-mingw32-gcc
PKG    := crosspoint-usb-wfx

.PHONY: all linux clean dist dist-linux dist-windows dist-windows-native

all: linux
linux: crosspoint.wfx
crosspoint.wfx: $(SRC) $(HDR)
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $(SRC)

dist: dist-linux
	@echo "For the Windows zip on Linux/WSL: 'make dist-windows'."
	@echo "On Windows with MSYS2: run build.bat."

dist-linux: $(SRC) $(HDR) $(EXTRA)
	rm -rf dist/linux && mkdir -p dist/linux
	$(CC) -shared -fPIC $(CFLAGS) -o dist/linux/crosspoint.wfx $(SRC)
	cp $(EXTRA) dist/linux/
	cd dist/linux && zip -j ../$(PKG)-linux-x86_64.zip crosspoint.wfx $(EXTRA)
	@echo "-> tools/wfx/dist/$(PKG)-linux-x86_64.zip"

# Cross-compile from Linux (needs gcc-mingw-w64 package).
dist-windows: $(SRC) $(HDR) $(EXTRA)
	rm -rf dist/win && mkdir -p dist/win
	$(WINCC32) -shared $(CFLAGS) -o dist/win/crosspoint.wfx   $(SRC) -static-libgcc -lsetupapi
	$(WINCC64) -shared $(CFLAGS) -o dist/win/crosspoint.wfx64 $(SRC) -static-libgcc -lsetupapi
	cp $(EXTRA) dist/win/
	cd dist/win && zip -j ../$(PKG)-windows.zip crosspoint.wfx crosspoint.wfx64 $(EXTRA)
	@echo "-> tools/wfx/dist/$(PKG)-windows.zip"

# Native Windows build from an MSYS2 shell. build.bat calls this target twice
# (once per arch); each invocation builds whichever arch the shell provides and
# drops it into dist/win so build.bat can zip them together.
dist-windows-native: $(SRC) $(HDR)
	mkdir -p dist/win
	$(CC) -shared $(CFLAGS) -o dist/win/$(NATIVE_OUT) $(SRC) -static-libgcc -lsetupapi

clean:
	rm -f crosspoint.wfx crosspoint.wfx64
	rm -rf dist
