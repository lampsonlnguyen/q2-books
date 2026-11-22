#!/usr/bin/env python3
"""Image must reach the actual Books page framebuffer, not just ZIP extraction."""
import subprocess
import sys
from pathlib import Path

exe, epub, pgm = sys.argv[1:]
result = subprocess.run([exe, "--render-images", epub, pgm],
                        text=True, capture_output=True, timeout=40)
assert result.returncode == 0, (result.returncode, result.stdout, result.stderr)
assert "Books rendered images: 2" in result.stdout, result.stdout
raw = Path(pgm).read_bytes()
header = b"P5\n375 320\n255\n"
assert raw.startswith(header), raw[:40]
frame = raw[len(header):]
assert len(frame) == 375 * 320
# Fixture contains PNG and JPEG; their grayscale output creates ink near the
# centered image location, underneath the chapter heading and first paragraph.
# Both images are bounded and halftoned; ensure the page contains ink.
assert sum(pixel < 255 for pixel in frame) > 30
print("Books image embedded, decoded and rasterized to Q2 framebuffer: OK")
