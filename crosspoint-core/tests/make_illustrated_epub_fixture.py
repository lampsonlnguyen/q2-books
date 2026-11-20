#!/usr/bin/env python3
"""Small illustrated EPUB fixture; no Pillow or network dependencies."""
import struct
import sys
import zlib
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED


def chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def png():
    # 2x2 RGB, red/green/blue/white; independently decodeable PNG.
    pixels = (b"\x00\xff\x00\x00\x00\xff\x00"
              b"\x00\x00\x00\xff\xff\xff\xff")
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", 2, 2, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels))
            + chunk(b"IEND", b""))


def main(out):
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:title>Illustrated CrossPod test</dc:title>
<dc:identifier>crosspod-images</dc:identifier><dc:language>en</dc:language></metadata>
<manifest><item id="chapter" href="text/chapter.xhtml" media-type="application/xhtml+xml"/>
<item id="image" href="images/test.png" media-type="image/png"/></manifest>
<spine><itemref idref="chapter"/></spine></package>"""
    chapter = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
<h1>Illustrated chapter</h1><p>Before the image.</p>
<img src="../images/test.png" alt="Four colored squares"/>
<p>After the image.</p></body></html>"""
    container = """<?xml version="1.0"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
<rootfiles><rootfile full-path="OEBPS/content.opf"
media-type="application/oebps-package+xml"/></rootfiles></container>"""
    with ZipFile(out, "w") as z:
        z.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
        z.writestr("META-INF/container.xml", container, compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/text/chapter.xhtml", chapter, compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/images/test.png", png(), compress_type=ZIP_DEFLATED)


if __name__ == "__main__":
    main(sys.argv[1])
