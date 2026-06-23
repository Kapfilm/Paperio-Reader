# CrossPoint USB — Total/Double Commander file-system plugin (WFX)

Browse and manage the e-reader's SD card from **Total Commander** (Windows) or
**Double Commander** (Windows/Linux) over the USB cable — list directories,
copy files to/from the device, rename/move, delete, and create folders.

It speaks the same serial protocol as [`../serial_transfer.py`](../serial_transfer.py),
which is wire-compatible with [MicroReader](https://github.com/CidVonHighwind/microreader)
by CidVonHighwind.

## Build

```sh
make            # Linux  -> crosspoint.wfx
make win32      # Windows 32-bit -> crosspoint.wfx     (needs i686-w64-mingw32-gcc)
make win64      # Windows 64-bit -> crosspoint.wfx64   (needs x86_64-w64-mingw32-gcc)
```

Files: `crosspoint.c` (the WFX plugin), `cp_serial.c/.h` (the serial transport,
shared protocol logic), `wfxplugin.h` (trimmed WFX API + cross-platform shims).

## Install

**Double Commander:** Configuration → Plugins → WFX → Add → pick `crosspoint.wfx`
(Linux) / `crosspoint.wfx`/`.wfx64` (Windows). It then appears in the drive/file-
system list as **CrossPoint USB**.

**Total Commander:** copy `crosspoint.wfx`/`.wfx64` into a folder, then
Configuration → Options → Plugins → File system plugins → Configure → Add, or
just open the `.wfx` from within TC to be prompted to install. Reach it via the
**Network Neighborhood** (`\\`) → **CrossPoint USB**.

## Use

1. On the reader: **File Transfer → USB Transfer** (this arms the connection).
2. Plug in USB and open **CrossPoint USB** in the commander.

Notes:
- Opening the port resets the ESP32-C3 once; the reader reboots straight back
  into the USB Transfer screen (~2 s) and the plugin waits for it. Keep only one
  program (this plugin **or** the CLI) talking to the port at a time.
- Linux serial access needs your user in the `dialout` group.
- The device path `/` is the SD-card root.

## Port selection

The reader is found automatically by its USB id (Espressif **303a:1001**), so it
works even with other serial gadgets plugged in. To force a specific port, set
the `CROSSPOINT_PORT` environment variable before launching the commander, e.g.
`CROSSPOINT_PORT=/dev/ttyACM1 doublecmd` (Linux) or `set CROSSPOINT_PORT=COM7`
(Windows). On Windows the auto-scan tries each COM port, so `CROSSPOINT_PORT` is
the reliable way to pick the right one.

## Limitations

- One connection at a time; throughput is ~90 KB/s (USB-CDC + per-chunk ACK).
- `FsRemoveDir` removes an *empty* directory (the commander empties it first).
