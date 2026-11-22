#!/usr/bin/env python3
"""Verify that the active Books EPUB reader uses original CrossPoint CSS."""
import subprocess
import sys

binary, epub = sys.argv[1:]
result = subprocess.run(
    [binary, "--inspect-styles", epub, "OEBPS/chapter1.xhtml"],
    check=True, capture_output=True, text=True, timeout=30
)
lines = result.stdout.splitlines()
assert len(lines) == 4, (lines, result.stderr)
highlight = next(line for line in lines if "Bold text" in line)
assert "align=2" in highlight, highlight  # CSS text-align:center
assert "bold=1" in highlight, highlight     # XHTML <strong>
assert "italic=1" in highlight, highlight # CSS and XHTML <em>
assert "SECRET_HIDDEN_PARAGRAPH" not in result.stdout
rtl = next(line for line in lines if "مرحبا بالعالم" in line)
assert "rtl=1" in rtl, rtl
assert "Nguyễn" in result.stdout
print("Books original CSS cascade, Unicode, RTL and hidden-text integration: OK")
