#!/usr/bin/env python3
"""Convert the exact 375x320 8-bit grayscale CrossPoint CI page to PNG.

No Pillow or system image libraries: only Python's stdlib, so the preview
artifact is reproducible on GitHub runners and the Q2 Linux host toolchain.
"""
import binascii
import struct
import sys
import zlib
from pathlib import Path


def chunk(tag: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + tag
        + payload
        + struct.pack(">I", binascii.crc32(tag + payload) & 0xFFFFFFFF)
    )


def convert(source: Path, target: Path) -> None:
    data = source.read_bytes()
    parts = data.split(b"\n", 3)
    if len(parts) != 4 or parts[:3] != [b"P5", b"375 320", b"255"]:
        raise ValueError("Expected a 375x320 binary PGM emitted by CrossPoint")
    width, height = 375, 320
    pixels = parts[3]
    if len(pixels) != width * height:
        raise ValueError(f"Incorrect pixel data size: {len(pixels)}")
    scanlines = b"".join(b"\0" + pixels[y * width:(y + 1) * width] for y in range(height))
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(scanlines, 9))
        + chunk(b"IEND", b"")
    )
    target.write_bytes(png)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: pgm_to_png.py <source.pgm> <target.png>")
    convert(Path(sys.argv[1]), Path(sys.argv[2]))
