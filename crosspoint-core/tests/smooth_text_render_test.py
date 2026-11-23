#!/usr/bin/env python3
"""Smooth text through the real reader: --render-smooth must give the same
ink footprint as --render, with grey edge levels inside it, and
BOOKS_SMOOTH_TEXT=0 must give plain black and white."""
import os
import subprocess
import sys
import tempfile
from collections import Counter

reader, book = sys.argv[1:]


def pixels(mode, out, extra=None):
    env = dict(os.environ, BOOKS_STATE_DIR=state, **(extra or {}))
    r = subprocess.run([reader, mode, book, out], capture_output=True, text=True, timeout=60, env=env)
    assert r.returncode == 0, (mode, r.stderr)
    data = open(out, "rb").read()
    return data[data.index(b"255\n") + 4:]


with tempfile.TemporaryDirectory(prefix="books-smooth-") as state:
    bw = pixels("--render", os.path.join(state, "bw.pgm"))
    smooth = pixels("--render-smooth", os.path.join(state, "smooth.pgm"))
    off = pixels("--render-smooth", os.path.join(state, "off.pgm"), {"BOOKS_SMOOTH_TEXT": "0"})
    assert len(bw) == len(smooth) == 375 * 320
    # The page's text area only (the bands above and below hold the status
    # and progress, which may be drawn in TrueType at any coverage).
    area = slice(40 * 375, 282 * 375)
    bw, smooth, off = bw[area], smooth[area], off[area]
    levels = Counter(smooth)
    assert set(levels) <= {0, 85, 170, 255}, levels
    assert levels[85] > 100 and levels[170] > 100, levels   # real grey edges
    # Same footprint: every BW ink pixel is ink or grey, every paper stays paper.
    assert all((b == 0) == (s != 255) for b, s in zip(bw, smooth))
    assert set(off) <= {0, 255} and off == bw
    share = (levels[85] + levels[170]) / (len(smooth) - levels[255])
    print(f"Books smooth text render: {share:.0%} of ink pixels are grey edges")
