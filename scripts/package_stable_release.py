#!/usr/bin/env python3
"""Package the already validated application and publish its exact hashes."""

import hashlib
import json
import os
from pathlib import Path
import shutil


def main():
    version = os.environ["CROSSPOINT_RELEASE_VERSION"]
    source = Path(".pio/build/gh_release")
    output = Path("dist")
    output.mkdir(exist_ok=True)
    for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "firmware.elf", "firmware.map"):
        shutil.copyfile(source / name, output / name)
    firmware = (output / "firmware.bin").read_bytes()
    if b"CrossPoint version: " + version.encode() + b"\0" not in firmware:
        raise SystemExit("Firmware does not contain the exact release version")
    shutil.copyfile(output / "firmware.bin", output / f"firmware-Paperio-Reader-{version}.bin")
    manifest = {
        "version": version,
        "source_commit": os.environ["GITHUB_SHA"],
        "file": "firmware.bin",
        "bytes": len(firmware),
        "sha256": hashlib.sha256(firmware).hexdigest(),
        "devices": ["X3", "X4"],
        "hardware_tested": False,
    }
    (output / "firmware-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    assets = sorted(path for path in output.iterdir() if path.is_file() and path.name != "SHA256SUMS")
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in assets
    ))


if __name__ == "__main__":
    main()
