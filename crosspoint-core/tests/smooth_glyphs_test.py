#!/usr/bin/env python3
"""Smooth book text through the real reader: with a TrueType file for the
reading font, glyphs are drawn at full coverage over CrossPoint's layout.

BOOKS_TEST_FONTS: the card's .cpfont folder; Liberation Sans stands in for
the Noto Sans TrueType (a different design, so the agreement floor is loose;
the device build checks the real Noto files much more tightly). Skips (77)
without either."""
import os
import pathlib
import re
import subprocess
import sys
import tempfile
from collections import Counter

SKIP = 77
reader, book = sys.argv[1:]
fonts = os.environ.get("BOOKS_TEST_FONTS")
liberation = None
for d in ("/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
          "/opt/npm-tools/node_modules/pdfjs-dist/standard_fonts"):
    if (pathlib.Path(d) / "LiberationSans-Regular.ttf").exists():
        liberation = pathlib.Path(d)
        break
if not fonts or not (pathlib.Path(fonts) / "sans-12.cpfont").exists() or not liberation:
    print("Smooth glyphs: SKIP (needs BOOKS_TEST_FONTS and Liberation Sans)")
    sys.exit(SKIP)

with tempfile.TemporaryDirectory(prefix="books-smooth-glyphs-") as tmp:
    tmp = pathlib.Path(tmp)
    state, ttf = tmp / "state", tmp / "ttf"
    state.mkdir()
    ttf.mkdir()
    (state / "fonts").symlink_to(pathlib.Path(fonts).resolve())
    for style, name in (("regular", "Regular"), ("bold", "Bold"), ("italic", "Italic"),
                        ("bolditalic", "BoldItalic")):
        source = liberation / f"LiberationSans-{name}.ttf"
        if source.exists():
            (ttf / f"sans-{style}.ttf").symlink_to(source)
    env = dict(os.environ, BOOKS_STATE_DIR=str(state), BOOKS_TTF_DIR=str(ttf))

    def run(args, extra=None):
        r = subprocess.run([reader, *args], capture_output=True, text=True, timeout=90,
                           env=dict(env, **(extra or {})))
        assert r.returncode == 0, (args, r.returncode, r.stderr[-2000:])
        return r.stdout + r.stderr

    out = run(["--smooth-check", book])
    em = re.search(r"Books smooth glyphs: sans regular at ([0-9.]+) px em", out)
    assert em and 18 < float(em.group(1)) < 32, out[-1500:]  # 12 pt
    iou = float(re.search(r"IoU ([0-9.]+)", out).group(1))
    assert iou >= 0.5, iou

    def page(extra=None):
        path = tmp / "page.pgm"
        run(["--render-smooth", book, str(path)], extra)
        data = path.read_bytes()
        return data[data.index(b"255\n") + 4:][40 * 375:282 * 375]

    smooth, twobit = page(), page({"BOOKS_SMOOTH_GLYPHS": "0"})
    assert len(Counter(smooth)) > 64, len(Counter(smooth))   # full coverage
    assert set(twobit) <= {0, 85, 170, 255}                  # CrossPoint's 2-bit
    print(f"Books smooth glyphs: em {float(em.group(1)):.2f} px, IoU {iou:.3f} "
          f"(Liberation over Noto), {len(Counter(smooth))} coverage levels")
