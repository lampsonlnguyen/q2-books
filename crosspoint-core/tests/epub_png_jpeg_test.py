#!/usr/bin/env python3
"""Exercise real PNG and JPEG decoding inside the Books EPUB reader."""
import subprocess
import sys
from pathlib import Path
reader, epub, output = sys.argv[1:]
result = subprocess.run([reader, "--render-images", epub, output],
                        capture_output=True, text=True, timeout=60, check=True)
assert "Books rendered images: 2" in result.stdout, result.stdout
data = Path(output).read_bytes()
assert data.startswith(b"P5\n375 320\n255\n"), data[:32]
assert len(data) == 120015, len(data)
assert data[15:].count(0) > 30
print("Active EPUB reader PNG and JPEG decode: PASS")
