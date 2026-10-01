# MicroReader Calibre plugin — Paperio adaptation

Original author: Patrick / CidVonHighwind.
Source: https://github.com/CidVonHighwind/microreader/tree/a4adfdb9a37b83bcb724cac3dc8990f335b49e8a/tools/calibre-plugin
Original ZIP: microreader.zip at that revision.

This separate computer-side tool is distributed under GPL-2.0 (LICENSE), not the firmware's MIT license. The bundled serial/ sources are unchanged pyserial 3.5, BSD license in LICENSE-pyserial.txt. All Python sources are included in the release ZIP and this directory.

Paperio changes: /books instead of /sdcard/books; serial control lines deasserted before open; bounded STATUS handshake after USB reset, timeout cleanup; description and version. Original name Microreader is retained for Calibre/pyserial bootstrap compatibility. Install only one Microreader variant.

EPUB only. Does not implement USB Mass Storage and does not require flashing MicroReader firmware. Real Calibre/device integration has not been hardware-tested. Run `python3 test_plugin.py`, then `python3 build.py OUTPUT.zip`.
