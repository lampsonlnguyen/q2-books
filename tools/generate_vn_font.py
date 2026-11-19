#!/usr/bin/env python3
"""Generate small embedded UTF-8 font bitmaps (not a runtime font dependency).

Source inspiration: CrossPoint Reader's SD-card font pipeline (MIT), adapted for
the Q2's ARGB framebuffer. Font data are generated from an installed OFL font.
"""
from pathlib import Path
import unicodedata
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
font_file = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
if not font_file.exists():
    raise SystemExit("Missing font: sudo apt install fonts-dejavu-core")
font = ImageFont.truetype(str(font_file), 15)
chars = {chr(i) for i in range(32, 127)}
chars.update(chr(i) for i in range(160, 384))
chars.update(chr(i) for i in range(0x1EA0, 0x1EFA))
chars.update(chr(i) for i in range(0x2000, 0x2070))
chars.update(chr(i) for i in range(0x0300, 0x0370))
chars.add("\ufffd")
chars = sorted(chars, key=ord)
data = []
entries = []
for ch in chars:
    # Fixed 19px cell top, preserving Vietnamese high/low accents.
    # Baseline at y=16. Unicode glyphs are rasterized once at build time.
    canvas = Image.new("L", (24, 20))
    draw = ImageDraw.Draw(canvas)
    draw.text((0, 0), ch, font=font, fill=255, anchor="lt")
    adv = max(1, min(24, round(font.getlength(ch))))
    bbox = canvas.getbbox()
    width = min(24, max(adv, (bbox[2] if bbox else 0)))
    width = max(1, width)
    offset = len(data)
    for yy in range(20):
        data.extend(canvas.crop((0, yy, width, yy+1)).tobytes())
    entries.append((ord(ch), offset, width, adv))
out = ROOT / "src/font_vn.h"
with out.open("w", encoding="utf-8") as handle:
    handle.write("/* Generated from DejaVu Sans (permissive font license). Do not edit. */\n")
    handle.write("#ifndef CROSSPOD_VN_FONT_H\n#define CROSSPOD_VN_FONT_H\n")
    handle.write("typedef struct { unsigned codepoint,offset; unsigned char width,advance; } CPGlyph;\n")
    handle.write("static const CPGlyph cp_glyphs[]={\n")
    for cp, offset, width, adv in entries:
        handle.write(f"{{{cp},{offset},{width},{adv}}},\n")
    handle.write("};\n")
    handle.write("static const unsigned char cp_bitmap[]={\n")
    for pos in range(0, len(data), 40):
        handle.write(",".join(map(str, data[pos:pos+40]))+",\n")
    handle.write("};\n")
    handle.write("#endif\n")
print(f"Created {out} with {len(entries)} glyphs, {len(data)} grayscale bytes")
