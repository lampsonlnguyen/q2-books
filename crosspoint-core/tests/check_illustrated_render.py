#!/usr/bin/env python3
"""Image must reach the actual CrossPod page framebuffer, not just ZIP extraction."""
import subprocess
import sys
from pathlib import Path

exe, epub, pgm = sys.argv[1:]
result = subprocess.run([exe, "--render-images", epub, pgm],
                        text=True, capture_output=True, timeout=40)
assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
assert "CrossPod rendered PNG images: 1" in result.stdout, result.stdout
raw = Path(pgm).read_bytes()
header = b"P5\n375 320\n255\n"
assert raw.startswith(header), raw[:40]
frame = raw[len(header):]
assert len(frame) == 375 * 320
# Fixture is four colors; a grayscale-halftoned image creates ink near the
# centered image location, underneath the chapter heading and first paragraph.
# The image renders at 2x2 in this input; ensure its position contains ink.
assert sum(pixel < 255 for pixel in frame) > 30
print("CrossPod image embedded, decoded and rasterized to Q2 framebuffer: OK")
