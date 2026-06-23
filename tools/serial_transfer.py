#!/usr/bin/env python3
"""CrossPoint USB serial file-transfer CLI.

Talks the serial protocol exposed by the firmware's "USB Transfer" screen
(File Transfer -> USB Transfer), which is wire-compatible with
CidVonHighwind/MicroReader. Lets you browse, upload, download, remove, rename
and mkdir on the device's SD card over the USB cable.

Usage (open the USB Transfer screen on the device first):
    serial_transfer.py [--port /dev/ttyACM0] status
    serial_transfer.py ls [/books]
    serial_transfer.py books
    serial_transfer.py get /books/foo.epub [foo.epub]
    serial_transfer.py put foo.epub [/books/foo.epub]
    serial_transfer.py rm /books/foo.epub
    serial_transfer.py mv /books/a.epub /books/b.epub
    serial_transfer.py mkdir /notes

Notes
- Opening the port hardware-resets the ESP32-C3; the device reboots straight
  back into the USB Transfer screen (~2s) and this tool waits that out. Keep one
  process talking to the port at a time.
- The port is opened with DTR/RTS deasserted (still resets, but avoids the
  esptool-style download reset).

Attribution: protocol by CidVonHighwind (https://github.com/CidVonHighwind/microreader).
"""
import argparse
import glob
import os
import struct
import sys
import time
import zlib

try:
    import serial  # pyserial
    import serial.tools.list_ports
except ImportError:
    sys.exit("pyserial is required: pip install pyserial")

CHUNK = 2048
ACK = 0x06
CMND = b"CMND"


class TransferError(Exception):
    pass


class CrossPointSerial:
    """Client for the CrossPoint serial file-transfer protocol.

    Open once, reuse for many operations: the first open resets the device, then
    the connection is stable. Use as a context manager.
    """

    def __init__(self, port=None, baud=115200, verbose=False):
        self.port_name = port or self._autodetect()
        self.baud = baud
        self.verbose = verbose
        self.s = None

    # -- connection ---------------------------------------------------------
    # ESP32-C3 native USB Serial/JTAG identity.
    ESP_VID, ESP_PID = 0x303A, 0x1001

    @classmethod
    def _autodetect(cls):
        # 1) explicit override via environment.
        env = os.environ.get("CROSSPOINT_PORT")
        if env:
            return env
        ports = list(serial.tools.list_ports.comports())
        # 2) the reader, matched by USB VID:PID (works with other serial gadgets plugged in).
        for p in ports:
            if (p.vid, p.pid) == (cls.ESP_VID, cls.ESP_PID):
                return p.device
        for p in ports:  # any Espressif device
            if p.vid == cls.ESP_VID:
                return p.device
        # 3) fallback: first CDC/USB serial device.
        for dev in sorted(glob.glob("/dev/ttyACM*")) + sorted(glob.glob("/dev/ttyUSB*")):
            return dev
        return ports[0].device if ports else "/dev/ttyACM0"

    def open(self, wait_timeout=25.0):
        # Construct without a port so DTR/RTS are deasserted *before* open().
        s = serial.Serial()
        s.port = self.port_name
        s.baudrate = self.baud
        s.timeout = 3.0
        s.write_timeout = 10.0
        s.dtr = False
        s.rts = False
        s.open()
        self.s = s
        time.sleep(0.2)
        s.reset_input_buffer()
        s.reset_output_buffer()
        if not self._wait_ready(wait_timeout):
            raise TransferError(
                "device did not respond. Is the 'USB Transfer' screen open?"
            )
        return self

    def close(self):
        if self.s:
            self.s.close()
            self.s = None

    def __enter__(self):
        return self.open()

    def __exit__(self, *exc):
        self.close()

    def _wait_ready(self, timeout):
        """Poll STATUS until the device replies (covers the open-reset reboot)."""
        end = time.time() + timeout
        while time.time() < end:
            self.s.reset_input_buffer()
            self.s.write(CMND + b"S")
            self.s.flush()
            line = self._read_until(("STATUS:",), timeout=1.5)
            if line is not None:
                return True
        return False

    # -- low-level io -------------------------------------------------------
    def _read_line(self, timeout=3.0):
        self.s.timeout = timeout
        return self.s.readline().decode("utf-8", "replace").rstrip("\r\n")

    def _read_until(self, prefixes, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            line = self._read_line(timeout)
            if line == "":
                continue
            if any(line.startswith(p) for p in prefixes):
                return line
        return None

    def _read_exact(self, n, timeout=30.0):
        self.s.timeout = timeout
        buf = b""
        while len(buf) < n:
            chunk = self.s.read(n - len(buf))
            if not chunk:
                raise TransferError(f"timeout reading {n} bytes (got {len(buf)})")
            buf += chunk
        return buf

    def _expect_ok(self, what):
        resp = self._read_until(("OK", "ERR:"), timeout=10.0)
        if resp != "OK":
            raise TransferError(f"{what} failed: {resp or '<no reply>'}")

    # -- operations ---------------------------------------------------------
    def status(self):
        self.s.reset_input_buffer()
        self.s.write(CMND + b"S")
        line = self._read_until(("STATUS:",), timeout=5.0)
        if line is None:
            raise TransferError("no STATUS reply")
        return line[len("STATUS:"):]

    def list_dir(self, path):
        """Return a list of dicts: {name, is_dir, size, mtime, path}."""
        self.s.reset_input_buffer()
        self.s.write(CMND + b"A" + struct.pack("<H", len(path.encode())) + path.encode())
        head = self._read_until(("DIR:", "ERR:"), timeout=5.0)
        if head is None or head.startswith("ERR:"):
            raise TransferError(f"ls {path} failed: {head or '<no reply>'}")
        entries = []
        end = time.time() + 15.0
        while time.time() < end:
            line = self._read_line(timeout=5.0)
            if line == "END":
                break
            if not line:
                continue
            parts = line.split("|")
            if parts[0] == "d" and len(parts) >= 2:
                entries.append({"name": parts[1], "is_dir": True, "size": 0, "mtime": 0,
                                "path": _join(path, parts[1])})
            elif parts[0] == "f" and len(parts) >= 2:
                size = int(parts[2]) if len(parts) > 2 and parts[2].isdigit() else 0
                mtime = int(parts[3]) if len(parts) > 3 and parts[3].lstrip("-").isdigit() else 0
                entries.append({"name": parts[1], "is_dir": False, "size": size,
                                "mtime": mtime, "path": _join(path, parts[1])})
        return entries

    def list_books(self):
        """Return a list of dicts: {path, title, author}."""
        self.s.reset_input_buffer()
        self.s.write(CMND + b"L")
        if self._read_until(("BOOKS:",), timeout=5.0) is None:
            raise TransferError("no BOOKS reply")
        books = []
        end = time.time() + 15.0
        while time.time() < end:
            line = self._read_line(timeout=5.0)
            if line == "END":
                break
            if not line:
                continue
            parts = line.split("|")
            books.append({"path": parts[0],
                          "title": parts[1] if len(parts) > 1 else "",
                          "author": parts[2] if len(parts) > 2 else ""})
        return books

    def download(self, remote, local):
        # Retry a few times: a download can occasionally stall on a transient
        # USB-CDC link hiccup and abort; the device recovers, so a re-request
        # succeeds.
        last = None
        for _ in range(3):
            try:
                return self._download_once(remote, local)
            except TransferError as e:
                last = e
                self.s.reset_input_buffer()
                time.sleep(0.1)
        raise last

    def _download_once(self, remote, local):
        self.s.reset_input_buffer()
        self.s.write(CMND + b"T" + struct.pack("<H", len(remote.encode())) + remote.encode())
        ready = self._read_until(("READY", "ERR:"), timeout=10.0)
        if ready != "READY":
            raise TransferError(f"download {remote} failed: {ready or '<no reply>'}")
        size = struct.unpack("<I", self._read_exact(4))[0]
        # ACK-paced: read each chunk, send 0x06, then the device sends the next.
        data = bytearray()
        remaining = size
        while remaining > 0:
            want = min(remaining, CHUNK)
            data += self._read_exact(want)
            self.s.write(bytes([ACK]))
            self.s.flush()
            remaining -= want
        crc = struct.unpack("<I", self._read_exact(4))[0]
        if (zlib.crc32(bytes(data)) & 0xFFFFFFFF) != crc:
            raise TransferError("download CRC mismatch")
        with open(local, "wb") as f:
            f.write(data)
        return size

    def upload(self, local, remote):
        data = open(local, "rb").read()
        crc = zlib.crc32(data) & 0xFFFFFFFF
        # 'W' = write to an arbitrary path (EPUB magic would force /books).
        hdr = CMND + b"W" + struct.pack("<H", len(remote.encode())) + remote.encode()
        hdr += struct.pack("<I", len(data))
        self.s.reset_input_buffer()
        self.s.write(hdr)
        ready = self._read_until(("READY", "ERR:"), timeout=10.0)
        if ready != "READY":
            raise TransferError(f"upload {remote} failed: {ready or '<no reply>'}")
        sent = 0
        while sent < len(data):
            end = min(sent + CHUNK, len(data))
            self.s.write(data[sent:end])
            self.s.flush()
            ack = self._read_exact(1)
            if ack != bytes([ACK]):
                raise TransferError(f"bad ACK {ack!r} at offset {sent}")
            sent = end
        self.s.write(struct.pack("<I", crc))
        self.s.flush()
        self._expect_ok(f"upload {remote}")
        return len(data)

    def remove(self, remote):
        self.s.reset_input_buffer()
        self.s.write(CMND + b"R" + struct.pack("<H", len(remote.encode())) + remote.encode())
        self._expect_ok(f"rm {remote}")

    def rename(self, src, dst):
        self.s.reset_input_buffer()
        self.s.write(CMND + b"N" + struct.pack("<H", len(src.encode())) + src.encode()
                     + struct.pack("<H", len(dst.encode())) + dst.encode())
        self._expect_ok(f"mv {src} -> {dst}")

    def mkdir(self, path):
        self.s.reset_input_buffer()
        self.s.write(CMND + b"K" + struct.pack("<H", len(path.encode())) + path.encode())
        self._expect_ok(f"mkdir {path}")


def _join(d, name):
    if not d or d == "/":
        return "/" + name
    return d.rstrip("/") + "/" + name


def _fmt_size(n):
    size = float(n)
    for unit in ("B", "K", "M", "G"):
        if size < 1024 or unit == "G":
            return f"{int(size)}{unit}" if unit == "B" else f"{size:.1f}{unit}"
        size /= 1024


def _human_time(t):
    if not t:
        return "-"
    return time.strftime("%Y-%m-%d %H:%M", time.localtime(t))


# --- CLI ---------------------------------------------------------------------
def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    p.add_argument("--port", help="serial port (default: auto-detect the reader by "
                                  "USB VID:PID 303a:1001, or $CROSSPOINT_PORT)")
    p.add_argument("--baud", type=int, default=115200)
    p.add_argument("-v", "--verbose", action="store_true")
    sub = p.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="device heap status")
    sp = sub.add_parser("ls", help="list a directory")
    sp.add_argument("path", nargs="?", default="/")
    sub.add_parser("books", help="list books (the L command)")
    sp = sub.add_parser("get", help="download a file")
    sp.add_argument("remote")
    sp.add_argument("local", nargs="?")
    sp = sub.add_parser("put", help="upload a file")
    sp.add_argument("local")
    sp.add_argument("remote", nargs="?")
    sp = sub.add_parser("rm", help="remove a file")
    sp.add_argument("remote")
    sp = sub.add_parser("mv", help="rename / move")
    sp.add_argument("src")
    sp.add_argument("dst")
    sp = sub.add_parser("mkdir", help="create a directory")
    sp.add_argument("path")
    sub.add_parser("shell", help="interactive session (one connection = one reboot)")
    args = p.parse_args(argv)

    try:
        with CrossPointSerial(args.port, args.baud, args.verbose) as cp:
            if args.cmd == "shell":
                _run_shell(cp, p)
                return 0
            return _dispatch(cp, args)
    except (TransferError, serial.SerialException) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1


def _run_shell(cp, parser):
    """REPL over a single connection: every one-shot CLI invocation opens the
    port (which resets the device), so batching commands here avoids a reboot
    per command. Type 'help' for commands, 'quit' to exit."""
    import shlex

    print("CrossPoint shell — status/ls/books/get/put/rm/mv/mkdir. 'quit' to exit.")
    while True:
        try:
            line = input("crosspoint> ").strip()
        except (EOFError, KeyboardInterrupt):
            print()
            return
        if not line:
            continue
        if line in ("quit", "exit", "q"):
            return
        if line in ("help", "?"):
            parser.print_help()
            continue
        try:
            args = parser.parse_args(shlex.split(line))
        except SystemExit:
            continue  # argparse already printed the error
        if args.cmd == "shell":
            continue
        try:
            _dispatch(cp, args)
        except (TransferError, serial.SerialException) as e:
            print(f"error: {e}", file=sys.stderr)


def _dispatch(cp, args):
    if args.cmd == "status":
        print(cp.status())
    elif args.cmd == "ls":
        entries = sorted(cp.list_dir(args.path), key=lambda e: (not e["is_dir"], e["name"].lower()))
        for e in entries:
            kind = "d" if e["is_dir"] else "-"
            size = "" if e["is_dir"] else _fmt_size(e["size"])
            print(f"{kind} {size:>8}  {_human_time(e['mtime']):>16}  {e['name']}")
        print(f"({len(entries)} entries)")
    elif args.cmd == "books":
        for b in cp.list_books():
            title = b["title"] or os.path.splitext(os.path.basename(b["path"]))[0]
            author = f" — {b['author']}" if b["author"] else ""
            print(f"{b['path']}\n    {title}{author}")
    elif args.cmd == "get":
        local = args.local or os.path.basename(args.remote)
        n = cp.download(args.remote, local)
        print(f"downloaded {args.remote} -> {local} ({n} bytes)")
    elif args.cmd == "put":
        remote = args.remote or ("/books/" + os.path.basename(args.local))
        n = cp.upload(args.local, remote)
        print(f"uploaded {args.local} -> {remote} ({n} bytes)")
    elif args.cmd == "rm":
        cp.remove(args.remote)
        print(f"removed {args.remote}")
    elif args.cmd == "mv":
        cp.rename(args.src, args.dst)
        print(f"moved {args.src} -> {args.dst}")
    elif args.cmd == "mkdir":
        cp.mkdir(args.path)
        print(f"created {args.path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
