# CrossPoint USB — Total/Double Commander file-system plugin (WFX)

Browse and manage the e-reader's SD card from **Total Commander** (Windows) or
**Double Commander** (Windows/Linux) over the USB cable — list directories,
copy files to/from the device, rename/move, delete, and create folders.

It speaks the same serial protocol as [`../serial_transfer.py`](../serial_transfer.py),
which is wire-compatible with [MicroReader](https://github.com/CidVonHighwind/microreader)
by CidVonHighwind.

## Build

**On Linux / WSL:**

```sh
make                 # native build -> crosspoint.wfx (what Double Commander loads here)
make dist-linux      # release zip:  dist/crosspoint-usb-wfx-linux-x86_64.zip
make dist-windows    # release zip:  dist/crosspoint-usb-wfx-windows.zip
```

`dist-windows` cross-compiles for Windows and needs the MinGW cross-compilers:
`sudo apt install gcc-mingw-w64 make zip`

**On Windows (MSYS2):**

1. Install [MSYS2](https://www.msys2.org/) (default path `C:\msys64`).
2. Open any MSYS2 shell and install the toolchains once:
   ```sh
   pacman -S mingw-w64-x86_64-gcc mingw-w64-i686-gcc make zip
   ```
3. Double-click **`build.bat`** (or run it from cmd). It produces
   `dist\crosspoint-usb-wfx-windows.zip` containing both the 32-bit `.wfx`
   and 64-bit `.wfx64` binaries.

Files: `crosspoint.c` (the WFX plugin), `cp_serial.c/.h` (the serial transport,
shared protocol logic), `wfxplugin.h` (trimmed WFX API + cross-platform shims),
`pluginst.inf` (the commander install manifest).

## Install

The release zips contain the binary plus `pluginst.inf`, so the easiest install
is the one-click path:

- **Double Commander / Total Commander:** open the matching `…-<os>.zip` *from
  within the commander* (navigate onto it and press Enter) — it reads
  `pluginst.inf` and offers to install. Then reach it via the file-system /
  Network Neighborhood (`\\`) list as **CrossPoint USB**.

Manual alternative (e.g. running from the build tree):

- **Double Commander:** Configuration → Plugins → WFX → Add → pick
  `crosspoint.wfx`.
- **Total Commander:** Configuration → Options → Plugins → File system plugins →
  Configure → Add, pointing at `crosspoint.wfx` / `.wfx64`.

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

The reader is found automatically by its USB id (Espressif **303a:1001**) on
both Windows and Linux, so it works even with other serial gadgets plugged in.
To force a specific port, in priority order:

1. **`CROSSPOINT_PORT` environment variable** — set it before launching the
   commander:
   - Linux: `CROSSPOINT_PORT=/dev/ttyACM1 doublecmd`
   - Windows: `set CROSSPOINT_PORT=COM7` in a cmd prompt, then start Total
     Commander from that same prompt.

2. **`Port=` in the plugin's ini file** — easier on Windows than environment
   variables. The ini file is the one Total/Double Commander assigns to the
   plugin; its path is shown in the plugin config dialog:
   - **Total Commander:** Configuration → Options → Plugins → File system
     plugins → select *CrossPoint USB* → the path appears next to "Ini file".
   - **Double Commander:** Configuration → Plugins → WFX → select the plugin
     → the ini path is shown in the details pane.

   Open that file in a text editor and add (or create it if missing):

   ```ini
   [crosspoint]
   Port=COM7
   ```

   Replace `COM7` with the actual port shown in Device Manager under
   *Ports (COM & LPT)* when the reader is plugged in and the USB Transfer
   screen is open.

3. Otherwise: USB VID:PID auto-detect picks the right port automatically on
   both Windows and Linux. A COM-scan fallback is used only if the SetupAPI
   lookup fails.

## Unicode

The plugin exports both the ANSI and wide-char (`Fs*W`) interfaces, so non-ASCII
file names work in both Double Commander and 64-bit Total Commander.

## Limitations

- One connection at a time; throughput is ~90 KB/s (USB-CDC + per-chunk ACK).
- `FsRemoveDir` removes an *empty* directory (the commander empties it first).
