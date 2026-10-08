<div align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="assets/paperio-reader-header-dark.svg">
    <source media="(prefers-color-scheme: light)" srcset="assets/paperio-reader-header.svg">
    <img src="assets/paperio-reader-header.svg" width="240" alt="Paper|io">
  </picture>
</div>

<p align="center">
  <img src="assets/screenshots/home.jpg" width="23%" alt="Paperio Reader home screen">
  <img src="assets/screenshots/recent-books.jpg" width="23%" alt="Recent books in a 2×2 grid">
  <img src="assets/screenshots/highlights.jpg" width="23%" alt="Three text highlighting styles">
  <img src="assets/screenshots/text-settings.jpg" width="23%" alt="Text settings with live preview">
</p>

<details open>
<summary><h2>Firmware Features</h2></summary>


Firmware for **XTEINK X3 and X4** e-readers.

Current version: **PaperioReader 2.1.8**.

- EPUB, FB2, FB2.ZIP, TXT, XTC/XTC-H, and Markdown.
- Fast rendering, background indexing, and cautious preloading of the next image.
- Footnotes, table of contents, link navigation, and return to the previous reading position.
- EPUB CSS and tables, floating images, hyphenation, superscript, and subscript.
- Custom fonts and a unified text settings screen with live preview. Font sizes can be selected by name or numerically; numeric mode is also available in per-book settings.
- Line spacing from 70% to 200% in 1% increments: a global option in “Text Settings” and a per-book option in “Quick Overrides” and “Book-specific overrides.”
- Dark theme on X4: while reading only or throughout the entire interface; covers and illustrations retain their original polarity.
- Continuous layout of original FB2 chapters: long chapters are not split into technical fragments that leave unused space at the bottom of a page, and the page counter covers the entire chapter.
- Mini and Lyra themes; recent books and library views with a 2×2 cover grid.
- Bookmarks, named pages, highlights, and a unified bookmark list for all books.
- Reading statistics, clock, weather, and KOReader synchronization.
- Configurable short, double, and long button presses.
- Transparent sleep screens, an information overlay, and a white background for both quick and full sleep.
- Wi-Fi, web-based file access, USB Serial transfer, firmware installation from an SD card, and over-the-air updates.
- Switching between Paperio and inkMOD in the second slot, with emergency recovery. Installation requirements and limitations are described below.

OTA updates are available under `Settings → System → System Update → Check for updates`.
For manual installation, use [firmware.bin version 2.1.8](https://github.com/Kapfilm/Paperio-Reader/releases/download/2.1.8/firmware.bin).

The changelog is available on the [Releases page](https://github.com/Kapfilm/Paperio-Reader/releases).


</details>

<details>
<summary><strong>Reading FB2</strong></summary>


Copy an `.fb2` or `.fb2.zip` book to the SD card and open it from the library.
A regular `.zip` containing an FB2 file is also recognized. If an archive
contains multiple FB2 files, the first one found is opened; use separate
archives for individual books.

<details>
<summary>Supported features</summary>


- Title, author, annotation, headings, and nested sections. The table of contents uses chapter titles from the book; an untitled section may be shown with a system-generated label.
- Embedded covers and illustrations. The cover is available on the home screen and in the library if it is present in the book and its image can be read.
- Basic text formatting: paragraphs, bold and italic emphasis, quotations, epigraphs, poetry, superscript, and subscript.
- Footnotes, links between sections, and links to individual text elements, including paragraphs and table cells. This makes it possible to read books containing numerous notes and cross-references.
- Notes window, nested navigation, opening the full text, and returning to the reading position.
- Global text settings, saved reading position, bookmarks, and highlights.
- UTF-8, Windows-1251, and KOI8-R; for legacy encodings, the correct encoding declaration in the FB2 file itself is important.


</details>

<details>
<summary>First and subsequent opening</summary>


When a book is opened for the first time, the firmware prepares it and creates
indexes on the SD card. A large book with thousands of sections, footnotes, or
images requires more time than a short novel; an archive also has to be
extracted. The card must have enough free space for the extracted book and
support files.

The preparation result is saved. When the book is opened again, a valid index
is used immediately, without displaying the “Indexing” screen. Covers and
indexes are not deleted merely because other books are opened. Clearing the
cache, damaged support files, or an index format update in a newer firmware
version may require the book to be prepared again. Whether the reading position
and bookmarks are preserved when the index format changes depends on the
firmware version; details of the transition from 2.1.6 are provided below.


</details>

<details>
<summary>Upgrading from 2.1.6 to 2.1.7</summary>


Previously opened FB2 books are prepared again once: technical fragments are
replaced with the original chapters. The old position is moved to the
**beginning of the original chapter** that contained the saved fragment, rather
than to the previous page.

Old bookmarks and highlights are not automatically transferred to the new
layout. Their data is preserved in a cache archive with the
`.before-native-chapters` suffix on the SD card and is no longer displayed as
active bookmarks or highlights. There is no automatic way to restore these
entries from the archive through the interface. Make a note of important
reading positions before updating. Book settings are preserved.

No additional migration is required when upgrading from either the
`2.1.7-fb2-chapters-test1` or `2.1.7-line-spacing-test1` test build.


</details>

<details>
<summary>Line spacing</summary>


`Text Settings → Layout → Reader Line Spacing` sets the global line spacing.
“Quick Overrides” and `Book-specific overrides → Reader Line Spacing` set the spacing for
the current book; “Default” restores the global value. The setting is available
for EPUB and FB2 reading, with a range of **70–200%**. The front buttons change
the value by **1%**, and the side buttons by **10%**. OK saves the selection;
“Back” cancels the change.


</details>

<details>
<summary>Limitations</summary>


- FB2 tables are displayed sequentially, cell by cell, with their text and links preserved. The original column grid is not reproduced.
- Book preparation and page layout are separate operations. Changing the font, font size, or spacing may require the pages to be laid out again; a pause may also occur when opening a fragment that has not yet been prepared. The absence of an additional indexing screen does not eliminate this work.
- Opening a note for the first time may require its text to be laid out. The opening time depends on the size of the fragment, its images, and the speed of the SD card.
- Navigation requires the link target to exist in the source FB2. The firmware cannot restore broken links or missing images in the book itself.


</details>

</details>

<details>
<summary><strong>Sample books: footnotes and notes</strong></summary>


**The Bible with commentaries by Blessed Theophylact of Bulgaria** is an example
of a large EPUB containing parallel passages, cross-references, and **6,268
commentaries**. The book is suitable for exploring the notes window, nested
navigation, and returning to the original verse in Paperio Reader.

**[Download the Bible with commentaries by Theophylact in EPUB format — 6.7 MB](https://github.com/Kapfilm/Paperio-Reader/raw/refs/heads/Paperio-Reader/examples/bible-theophylact.epub)**

The EPUB version preserves large centered headings, spacing between headings
and text, italic quotations, and backlinks. Internal page grouping is optimized
for navigation; faster opening of commentaries has been confirmed on the
XTEINK X4 with Paperio. EPUBCheck: no errors or warnings.

**The Bible with commentaries by John Chrysostom** is an especially demanding
example containing numerous footnotes, notes, and cross-references, suitable
for testing the processing speed of large books.

**[Download the Bible with commentaries by John Chrysostom in FB2.ZIP format — 7.8 MB](https://github.com/Kapfilm/Paperio-Reader/raw/refs/heads/Paperio-Reader/examples/bible-zlatoust.fb2.zip)**

The archive contains a single 32.0 MB FB2 file.

Copy the selected file to the SD card and open it in Paperio Reader. Select a
parallel-passage link or commentary number to open the note; the “Back” button
returns you to the reading position.


</details>

<details>
<summary><strong>Text highlighting</strong></summary>


Three highlighting styles are available in books:

- **Marker (light gray)** — subtle highlighting without darkening the text.
- **Black marker** — high-contrast highlighting with white text.
- **Underline** — preserves the original page background.

Select the desired style through `Book Menu → Highlight Text`; the default
style can be selected in the reading settings. Highlighting can also be
assigned to a short, double, or long press of a side button.


</details>

<details>
<summary><strong>Dictionaries</strong></summary>


Paperio Reader supports **StarDict** dictionaries. Extract each dictionary into
a separate folder on the SD card:

```text
/dictionaries/Dictionary name/
├── dictionary.idx
├── dictionary.dict       or dictionary.dict.dz
└── dictionary.ifo        optional
```

The `.idx` file and the selected `.dict` or `.dict.dz` file in the same folder
must have the same base name. On the first lookup, the firmware automatically
creates a fast `.qidx` index alongside them; this may take some time for a large
dictionary. If a compressed `.dict.dz` file does not open, use an uncompressed
`.dict` file.

You can select a dictionary through `Settings → Reading → Dictionary` or
directly from the open book menu by choosing `Select dictionary`.

If **“Automatic”** is selected, the firmware first searches the most suitable
dictionaries based on the alphabet used in the query, then checks the remaining
installed dictionaries if necessary. This is convenient if the card contains,
for example, a Russian explanatory dictionary and an English–Russian
dictionary.

To look up a word, open the book menu, select `Dictionary lookup`, and
select the desired word. This action can also be assigned to a short, double,
or long press of any button in the control settings.


</details>

<details>
<summary><strong>Installation and updates</strong></summary>


<details>
<summary>Devices with USB lock (Xteink Unlocker)</summary>


Some Xteink devices purchased from third-party stores (for example, on
AliExpress) come with a factory USB lock. If your device is locked, you will
need to use the
[Xteink Unlocker](https://crosspointreader.com/#unlock-tool) before you can
flash Paperio Reader.

Install the first version of Paperio Reader manually using the `firmware.bin`
file. Subsequent versions can be installed on the device via:

`Settings → System → System Update → Check for updates`

- [Latest release](https://github.com/Kapfilm/Paperio-Reader/releases/latest)
- [Download firmware.bin](https://github.com/Kapfilm/Paperio-Reader/releases/latest/download/firmware.bin)


</details>

</details>

<details>
<summary><strong>Switching between Paperio and inkMOD</strong></summary>


Starting with version 2.1.4, **Settings → System → System Update → Switch firmware slot** is available. It displays the name and version of the image in the second slot and checks the ESP32-C3 header, segment boundaries, checksum, and SHA-256 when present. After confirmation, the reader restarts into the other installed firmware. The firmware partitions are not overwritten. An empty or corrupted image cannot be selected.

Before using this feature for the first time, both firmware images must be installed once in separate slots:

1. Install [Paperio 2.1.8](https://github.com/Kapfilm/Paperio-Reader/releases/download/2.1.8/firmware.bin) using the regular SD update and leave a normal operating screen open for at least one minute.
2. Use the SD update in Paperio to install a compatible **inkMOD application image** for your reader. It will occupy the second slot, while Paperio remains in the first. Use an inkMOD version that supports slot switching. A full flash image containing the bootloader and partition table is not suitable for this operation.
3. In inkMOD, select the option to switch to the other slot. Paperio will start. To return to inkMOD, use the new “Switch firmware slot” option in Paperio.

If both firmware images are already installed, simply switch between them. Installing a new Paperio version from an older Paperio version will overwrite inkMOD in the occupied second slot; in that case, install inkMOD again as described in step 2.

**Limitations:** two slots of 0x640000 bytes (6.25 MiB) are available. A regular update replaces the firmware in the second slot: there is no permanent third backup copy. Paperio and [inkMOD](https://github.com/alpzoloto-sudo/inkmod/tree/b327a0887fa02d0dcd6c38e0808341d2b4a1fc2a) have been verified to use the same `partitions.csv` layout; arbitrary alternative layouts are not supported. An integrity check does not guarantee compatibility of settings, the SD cache, or third-party firmware behavior.

Manual switching and emergency recovery are separate operations. When a verified image is selected manually, it is marked as the chosen image without bootloader trial status, because third-party firmware may not support ESP-IDF confirmation. This does not confirm its stable operation and does not automatically add it as a trusted Paperio backup. Regular Paperio updates continue to use successful-boot confirmation and emergency recovery as described below. Automatic return from an inkMOD failure depends on inkMOD itself and the bootloader.

A user confirmed that switching works on a physical reader with test build 2.1.3-dualboot-test1, on which this release is based. The switcher concept and compatible partition layout come from the [alpzoloto-sudo/inkmod](https://github.com/alpzoloto-sudo/inkmod) project.


</details>

<details>
<summary><strong>Emergency recovery</strong></summary>


<details>
<summary>When a boot is considered successful</summary>


After an update, leave a normal operating screen open for at least **60 seconds**. Confirmation occurs after a regular screen has been rendered and the main loop has operated steadily for one minute. The boot logo, sleep mode, and recovery screen do not count as a successful boot. A long main-loop pause or incomplete rendering delays confirmation. In the USB log, confirmation is indicated by `Firmware confirmed after healthy UI/main-loop interval`.

Entering normal sleep or performing a regular restart before confirmation does not mark the firmware as healthy: trial status remains until a complete normal boot has succeeded.


</details>

<details>
<summary>What happens after crashes</summary>


The firmware stores information about the current image, the verified previous version, and the crash sequence in NVS service storage. After **three consecutive crash restarts without a successful operating interval**, it attempts to start the previous working version. Software crashes (PANIC), watchdog resets, and CPU lockups are counted. Sleep, regular restarts, power loss, and voltage drops do not increase this counter. A stable operating interval clears the crash sequence.

Rollback is possible only to a known, verified copy: its identifier, boot records, and image integrity are checked. The mere presence of a second partition is not sufficient—it may contain different firmware, such as USB firmware. Before switching, protection against an endless loop between two faulty versions is saved.

If no suitable copy exists, the copy is corrupted, or the rollback state cannot be saved safely, the **SD-card recovery screen** opens instead of switching. The user selects and confirms a firmware file. Books and bookmarks are not used as a firmware backup; this mechanism does not repair a corrupted card.


</details>

<details>
<summary>How to recover manually from the SD card</summary>


1. Download a compatible `firmware.bin` from the [required release](https://github.com/Kapfilm/Paperio-Reader/releases) and copy it to the SD card. Placing the file in the root directory is convenient, but the browser also lets you select a `.bin` file from a folder; the file does not have to be named `firmware.bin`.
2. When powering on or waking the device with the **power button**, hold the **UP** side button (the left side button) until the firmware selection screen appears. Connecting USB alone does not activate this mode.
3. Select the BIN file, wait for verification, and confirm installation. Do not disconnect power or remove the card while the image is being written.
4. After the image has been written successfully, the device will restart automatically. Leave a regular operating screen open for at least one minute to confirm the version.

If the application does not reach early initialization, or if the display or SD card is not working, this mode may be unavailable. Recovery via USB/the bootloader using a method suitable for the device will then be required. Software protection is not a substitute for a functioning bootloader and does not guarantee recovery from every failure.


</details>

<details>
<summary>First upgrade from 2.1.0 and limitations</summary>


**When installing 2.1.1 over 2.1.0 for the first time, automatic rollback to 2.1.0 using the new counter is not guaranteed.** Version 2.1.0 did not record the information needed to trust the backup image. After 2.1.1 has run successfully for one minute, the next update performed through it will be able to preserve this version as the verified previous version. Subsequent updates may overwrite the backup partition. Do not erase NVS or change the partition table during a regular update.

An ESP-IDF bootloader with rollback support enabled may restore unconfirmed firmware after the first crash reset, before the application counter takes effect. This depends on the installed bootloader; a regular `firmware.bin` does not update it. Crashes that occur before the application starts are not counted by the application.

The logic has been verified with desktop tests using models of flash memory, NVS, and boot records. Real crash rollback scenarios on X3/X4 hardware still require device testing. [Technical description](docs/boot-recovery.md).


</details>

</details>

<details>
<summary><strong>File transfer over USB</strong></summary>


Starting with version 2.1.2, select `Settings → System → System Update → USB Transfer`. The same mode is available from the connection menu. Leave this screen open and connect the reader using a data-capable cable.

**This is USB Serial, not USB mass storage:** the reader does not appear as a regular drive in Finder or File Explorer. Transfers are performed through Calibre with a plugin or through the utility. Selecting this option no longer changes the boot partition. When software connects to the serial port, the ESP32-C3 may restart at the hardware level; the reader should return to the transfer screen in the current firmware. Close other programs using this port, including Serial Monitor. When the transfer is complete, you can exit using the “Back” button.

<details>
<summary>Download the software</summary>


- **[serial_transfer.py — download file](https://github.com/Kapfilm/Paperio-Reader/releases/download/2.1.7/serial_transfer.py)** — upload and download files, list folders, create folders, rename, and delete. Suitable for FB2, FB2.ZIP, EPUB, and other files; [source code](tools/serial_transfer.py).
- **[microreader-paperio.zip — Calibre plugin for Paperio](https://github.com/Kapfilm/Paperio-Reader/releases/download/2.1.7/microreader-paperio.zip)** — a dedicated adaptation for transferring EPUB files. Do not extract the ZIP when installing it in Calibre.
- [Original MicroReader ZIP](https://raw.githubusercontent.com/CidVonHighwind/microreader/a4adfdb9a37b83bcb724cac3dc8990f335b49e8a/tools/calibre-plugin/microreader.zip) and [the project by CidVonHighwind](https://github.com/CidVonHighwind/microreader). For Paperio, use the adapted file above: the original uses a different root path and connection handshake.

The MicroReader firmware is **not required** to use the plugin: do not install it in place of Paperio. The plugin runs on the computer. Attribution and the tool’s separate license are provided in [tools/calibre-plugin](tools/calibre-plugin).


</details>

<details>
<summary>Calibre: use without a terminal</summary>


1. Install [Calibre](https://calibre-ebook.com/download) (the plugin requires version 5 or later).
2. Download `microreader-paperio.zip` from the link above. If your browser extracted the ZIP automatically, save the original archive again.
3. In Calibre, open `Preferences → Plugins → Load plugin from file`, select the ZIP, and confirm the installation. Restart Calibre. The plugin name remains **Microreader**; do not install the original and adapted versions at the same time.
4. Open “USB Transfer” on the reader, connect the cable, and wait for Calibre to detect the device. It may appear as Microreader in the interface.
5. Add an EPUB to your Calibre library, select the book, and choose **“Send to device”**. Wait for the job to complete. The book is saved to `/books` on the SD card.
6. Use the **“Device”** button to view the list. After the transfer, eject the device in Calibre, then exit transfer mode on the reader.

**Plugin limitation: EPUB.** For FB2 and FB2.ZIP, use the utility below or another method of copying files to the SD card. Do not rename an FB2 file to EPUB. If Calibre offers to convert a book, that is a separate Calibre operation, not a transfer of the original FB2. The adaptation is verified by software tests; interaction between a specific Calibre version, macOS, and the reader still requires testing on a device.


</details>

<details>
<summary>serial_transfer.py on macOS</summary>


Download the script from the link above and keep it in the **Downloads** folder. Python 3 is required; you can check whether it is installed with `python3 --version`. If Python is not installed, download it from [python.org](https://www.python.org/downloads/macos/).

Open the **Terminal** application. Create a dedicated environment and install `pyserial` once:

```bash
mkdir -p "$HOME/Paperio-USB"
cp "$HOME/Downloads/serial_transfer.py" "$HOME/Paperio-USB/"
cd "$HOME/Paperio-USB"
python3 -m venv .venv
source .venv/bin/activate
python -m pip install pyserial
```

For subsequent launches, the following is sufficient:

```bash
cd "$HOME/Paperio-USB"
source .venv/bin/activate
```

Open “USB Transfer” on the reader, connect the cable, and check the connection:

```bash
python serial_transfer.py status
python serial_transfer.py ls /
```

`status` prints the device response, while `ls /` lists the contents of the SD card root. Transfer examples (replace the book names with your own):

```bash
# Upload an FB2 file from the desktop to the SD card root:
python serial_transfer.py put "$HOME/Desktop/Book.fb2" "/Book.fb2"

# Upload an FB2 archive without changing its format:
python serial_transfer.py put "$HOME/Desktop/Book.fb2.zip" "/Book.fb2.zip"

# Download a book from the SD card to the desktop:
python serial_transfer.py get "/Book.fb2" "$HOME/Desktop/Book-from-reader.fb2"

# List the contents of a folder:
python serial_transfer.py ls /books
```

Paths such as `/books` refer to the SD card; `$HOME/Desktop` refers to the Mac. An existing file at the same destination path may be overwritten; specify a different name to keep both copies.


</details>

<details>
<summary>If the port is not found</summary>


```bash
python -m serial.tools.list_ports
```

Find the connected reader’s port, for example `/dev/cu.usbmodem101`, and pass **your port** with the `--port` option:

```bash
python serial_transfer.py --port /dev/cu.usbmodem101 status
python serial_transfer.py --port /dev/cu.usbmodem101 ls /
```

If several serial devices are connected, specify the port explicitly. If you receive an access error, close Calibre, Serial Monitor, and any other programs communicating with the reader. If no port is present, check the cable, USB connector, and whether the transfer screen is open. If there is no response after the reader restarts, wait for it to return to this screen and run the command again.


</details>

<details>
<summary>Multiple operations over one connection</summary>


Interactive mode keeps the connection open between commands:

```bash
python serial_transfer.py shell
```

At the `crosspoint>` prompt, you can enter:

```text
ls /
mkdir /books
put "/Users/yourname/Desktop/Book.fb2" "/books/Book.fb2"
ls /books
quit
```

Replace `yourname` with your account name. The `help` command displays help, and `quit` ends the session. The `rm` and `mv` commands delete and rename files—use them only when necessary. To view **all** files, use `ls`: the `books` command lists EPUB files for plugin compatibility.


</details>

</details>

<details>
<summary><strong>Build</strong></summary>


```sh
pio run -e gh_release
```

A release is created by pushing a version-number tag, such as `2.1.8`. GitHub Actions attaches
`firmware-Paperio-Reader-2.1.8.bin` and an OTA-compatible copy named `firmware.bin`.


</details>

<details>
<summary><strong>Paperio Reader Authors</strong></summary>


- **[Vladimir (@Kapfilm)](https://github.com/Kapfilm)** — author and maintainer of Paperio Reader: project development, feature selection, on-device testing, and releasing updates.
- **Codex (OpenAI)** — AI development assistant: code analysis and preparation of fixes, new features, and documentation in collaboration with the project author.


</details>

<details>
<summary><strong>Acknowledgments</strong></summary>


Special thanks to the developer of **[inkMOD — @alpzoloto-sudo](https://github.com/alpzoloto-sudo/inkmod)** for the open-source FB2 implementation. Paperio Reader uses parts of the inkMOD source code: the FB2 parser, XML tokenizer, ZIP/DEFLATE reader, Base64 decoder, reader adapters, and encoding tables. This foundation was adapted for Paperio Reader and further improved with fixes for indexing, covers, and footnotes.

The upstream version and commit are listed in the [FB2 code provenance document](lib/Fb2/UPSTREAM.md). The MIT license terms and copyright notices have been preserved. Thank you for creating work that can be studied and built upon.


</details>

<details>
<summary><strong>Project Foundation</strong></summary>


Paperio Reader continues the proven codebase of
[Witch Reader](https://github.com/jpirnay/witchhunt-reader), which is based on
[CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), and
uses the [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk).

The project is distributed under the [MIT License](LICENSE). Copyright notices and
licenses for the components used by the project are preserved in the source code.

</details>
