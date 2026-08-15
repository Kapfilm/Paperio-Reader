#!/usr/bin/env python3
"""Build compact .qidx/.sidx sidecars used by Witchhunt Reader.

StarDict .idx records contain a NUL-terminated UTF-8 headword followed by an
eight-byte offset/length pair.  StarDict .syn records use a four-byte index
ordinal instead.  The firmware stores the byte offset of every 256th record so
it can binary-search large files without first scanning them on the ESP32-C3.
"""

from __future__ import annotations

import argparse
import os
import struct
from pathlib import Path


QIDX_MAGIC = 0x58444951
SIDX_MAGIC = 0x58444953
SIDECAR_VERSION = 1
SAMPLE_INTERVAL = 256
HEADER = struct.Struct("<5I")
OFFSET = struct.Struct("<I")
READ_SIZE = 1024 * 1024


def build(
    source_path: Path, output_path: Path, magic: int, suffix_bytes: int
) -> tuple[int, int]:
    source_size = source_path.stat().st_size
    if source_size > 0xFFFFFFFF:
        raise ValueError("source is larger than the firmware's 32-bit file format")

    temp_path = output_path.with_suffix(output_path.suffix + ".tmp")
    entries = 0
    samples = 1
    suffix_bytes_left = 0
    absolute_position = 0

    try:
        with source_path.open("rb") as source, temp_path.open("wb+") as output:
            output.write(b"\0" * HEADER.size)
            output.write(OFFSET.pack(0))

            while chunk := source.read(READ_SIZE):
                for chunk_position, byte in enumerate(chunk):
                    if suffix_bytes_left:
                        suffix_bytes_left -= 1
                        if suffix_bytes_left == 0:
                            entries += 1
                            next_record = absolute_position + chunk_position + 1
                            if entries % SAMPLE_INTERVAL == 0 and next_record < source_size:
                                output.write(OFFSET.pack(next_record))
                                samples += 1
                    elif byte == 0:
                        suffix_bytes_left = suffix_bytes
                absolute_position += len(chunk)

            if suffix_bytes_left:
                raise ValueError(f"truncated {source_path.suffix} record")

            output.seek(0)
            output.write(
                HEADER.pack(magic, SIDECAR_VERSION, SAMPLE_INTERVAL, samples, source_size)
            )
            output.flush()
            os.fsync(output.fileno())

        temp_path.replace(output_path)
    except BaseException:
        temp_path.unlink(missing_ok=True)
        raise

    return entries, samples


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="input StarDict .idx or .syn file")
    parser.add_argument("output", type=Path, nargs="?", help="output .qidx or .sidx file")
    args = parser.parse_args()

    source = args.source.resolve()
    if not source.is_file():
        parser.error(f"source file does not exist: {source}")
    if source.suffix == ".idx":
        output_suffix, magic, suffix_bytes = ".qidx", QIDX_MAGIC, 8
    elif source.suffix == ".syn":
        output_suffix, magic, suffix_bytes = ".sidx", SIDX_MAGIC, 4
    else:
        parser.error("source must have an .idx or .syn extension")

    output = (args.output or source.with_suffix(output_suffix)).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    entries, samples = build(source, output, magic, suffix_bytes)
    print(f"Created {output} ({entries} entries, {samples} samples)")


if __name__ == "__main__":
    main()
