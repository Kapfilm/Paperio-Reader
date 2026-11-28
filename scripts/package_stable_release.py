#!/usr/bin/env python3
"""Package the already validated application and publish its exact hashes."""

import hashlib
import json
import os
from pathlib import Path
import shutil


def main():
    version = os.environ["CROSSPOINT_RELEASE_VERSION"]
    build_dir = Path(os.environ.get("PLATFORMIO_BUILD_DIR", ".pio/build"))
    source = build_dir / "gh_release"
    output = Path(os.environ.get("CROSSPOINT_DIST_DIR", "dist"))
    output.mkdir(parents=True, exist_ok=True)
    for name in ("firmware.bin", "bootloader.bin", "partitions.bin", "firmware.elf", "firmware.map"):
        shutil.copyfile(source / name, output / name)
    shutil.copyfile(build_dir / "x4c_release/firmware.bin", output / "firmware-x4-classic.bin")
    for model in ("x3", "x4"):
        shutil.copyfile(build_dir / f"{model}_release/firmware.bin", output / f"firmware-{model}.bin")
    shutil.copyfile(output / "firmware-x4-classic.bin", output / "firmware-x4c.bin")
    if (output / "firmware-x3.bin").read_bytes() == (output / "firmware-x4.bin").read_bytes():
        raise SystemExit("X3 and X4 must be separately compiled model-specific images")
    version_marker = b"CrossPoint version: " + version.encode() + b"\0"
    applications = []
    for name, devices in (
        ("firmware.bin", ["X3", "X4"]),
        ("firmware-x3.bin", ["X3"]),
        ("firmware-x4.bin", ["X4"]),
        ("firmware-x4-classic.bin", ["X4 Classic"]),
        ("firmware-x4c.bin", ["X4 Classic"]),
    ):
        firmware = (output / name).read_bytes()
        if version_marker not in firmware:
            raise SystemExit(f"{name} does not contain the exact release version")
        classic = "X4 Classic" in devices
        limit = 0x7e0000 if classic else 0x640000
        if len(firmware) > limit:
            raise SystemExit(f"{name} exceeds its application partition")
        if b"/paperio-x4-display.log" in firmware:
            raise SystemExit(f"{name} contains diagnostic SD logging")
        applications.append({
            "file": name,
            "bytes": len(firmware),
            "sha256": hashlib.sha256(firmware).hexdigest(),
            "devices": devices,
            "hardware_tested": False,
        })
    shutil.copyfile(output / "firmware.bin", output / f"firmware-Paperio-Reader-{version}.bin")
    manifest = {
        "version": version,
        "source_commit": os.environ["GITHUB_SHA"],
        **applications[0],
        "applications": applications,
    }
    (output / "firmware-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    assets = sorted(path for path in output.iterdir() if path.is_file() and path.name != "SHA256SUMS")
    (output / "SHA256SUMS").write_text("".join(
        f"{hashlib.sha256(path.read_bytes()).hexdigest()}  {path.name}\n" for path in assets
    ))


if __name__ == "__main__":
    main()
