# Build / package the CrossPoint USB WFX plugin for Total / Double Commander.
#
#   make                # native build -> crosspoint.wfx (what Double Commander loads here)
#   make dist-linux     # release zip: crosspoint-usb-wfx-linux-x86_64.zip
#   make dist-windows   # release zip: crosspoint-usb-wfx-windows.zip (needs mingw)
#   make clean
#
# A release zip contains the plugin binary + pluginst.inf so Total/Double
# Commander can install it with one click (open the zip from within the
# commander). The .wfx is a native binary, so each OS gets its own zip.

SRC    := crosspoint.c cp_serial.c
HDR    := cp_serial.h wfxplugin.h
EXTRA  := pluginst.inf README.md
CFLAGS := -O2 -Wall -Wextra -Wno-format-truncation -fvisibility=hidden
WINCC32 := i686-w64-mingw32-gcc
WINCC64 := x86_64-w64-mingw32-gcc
PKG    := crosspoint-usb-wfx

.PHONY: all linux clean dist dist-linux dist-windows

all: linux
linux: crosspoint.wfx
crosspoint.wfx: $(SRC) $(HDR)
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $(SRC)

dist: dist-linux
	@echo "For the Windows zip: 'make dist-windows' (needs i686-/x86_64-w64-mingw32-gcc)."

dist-linux: $(SRC) $(HDR) $(EXTRA)
	rm -rf dist/linux && mkdir -p dist/linux
	$(CC) -shared -fPIC $(CFLAGS) -o dist/linux/crosspoint.wfx $(SRC)
	cp $(EXTRA) dist/linux/
	cd dist/linux && zip -j ../$(PKG)-linux-x86_64.zip crosspoint.wfx $(EXTRA)
	@echo "-> tools/wfx/dist/$(PKG)-linux-x86_64.zip"

dist-windows: $(SRC) $(HDR) $(EXTRA)
	rm -rf dist/win && mkdir -p dist/win
	$(WINCC32) -shared $(CFLAGS) -o dist/win/crosspoint.wfx   $(SRC) -static-libgcc
	$(WINCC64) -shared $(CFLAGS) -o dist/win/crosspoint.wfx64 $(SRC) -static-libgcc
	cp $(EXTRA) dist/win/
	cd dist/win && zip -j ../$(PKG)-windows.zip crosspoint.wfx crosspoint.wfx64 $(EXTRA)
	@echo "-> tools/wfx/dist/$(PKG)-windows.zip"

clean:
	rm -f crosspoint.wfx crosspoint.wfx64
	rm -rf dist
