# Build the CrossPoint USB WFX plugin for Total Commander / Double Commander.
#
#   make            # native build (Linux: crosspoint.wfx)
#   make win32      # Windows 32-bit: crosspoint.wfx   (needs i686-w64-mingw32-gcc)
#   make win64      # Windows 64-bit: crosspoint.wfx64 (needs x86_64-w64-mingw32-gcc)
#   make all        # everything available
#   make clean

SRC := crosspoint.c cp_serial.c
CFLAGS := -O2 -Wall -Wextra -Wno-format-truncation -fvisibility=hidden

.PHONY: all linux win32 win64 clean
all: linux

linux: crosspoint.wfx
crosspoint.wfx: $(SRC) cp_serial.h wfxplugin.h
	$(CC) -shared -fPIC $(CFLAGS) -o $@ $(SRC)

win32: crosspoint32.wfx
crosspoint32.wfx: $(SRC) cp_serial.h wfxplugin.h
	i686-w64-mingw32-gcc -shared $(CFLAGS) -o crosspoint.wfx $(SRC) -static-libgcc

win64: crosspoint64.wfx64
crosspoint64.wfx64: $(SRC) cp_serial.h wfxplugin.h
	x86_64-w64-mingw32-gcc -shared $(CFLAGS) -o crosspoint.wfx64 $(SRC) -static-libgcc

clean:
	rm -f crosspoint.wfx crosspoint.wfx64 crosspoint32.wfx crosspoint64.wfx64
