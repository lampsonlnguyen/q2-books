#!/usr/bin/env python3
"""Regression: the active reader resolves CSS and images relative to XHTML."""
import subprocess
import sys

reader, epub = sys.argv[1:]
cmd = [reader, "--inspect-styles", epub, "OEBPS/text/chapter.xhtml"]
output = subprocess.run(cmd, check=True, capture_output=True,
                        text=True, timeout=30).stdout
caption = next((line for line in output.splitlines()
                if "Before the image." in line), None)
assert caption is not None, output
assert "align=2" in caption, caption  # original CrossPoint centered CSS
assert "italic=1" in caption, caption
assert "After the image." in output
assert "1. First item" in output, output
assert "2. Second item" in output, output
assert "Before break" in output and "After break" in output, output
assert not any("Before breakAfter break" in line for line in output.splitlines()), output
print("Sibling-directory CSS applied in active XHTML reader: PASS")
