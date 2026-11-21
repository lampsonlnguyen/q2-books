#!/usr/bin/env python3
"""Small illustrated EPUB fixture; no Pillow or network dependencies."""
import struct
import base64
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
<item id="image" href="images/test.png" media-type="image/png"/>\n<item id="jpeg" href="images/sample.jpg" media-type="image/jpeg"/></manifest>
<spine><itemref idref="chapter"/></spine></package>"""
    # 4x4 original JPEG fixture (created with Pillow; no network dependency).\n    jpeg = base64.b64decode("/9j/4AAQSkZJRgABAQAAAQABAAD/2wBDAAUDBAQEAwUEBAQFBQUGBwwIBwcHBw8LCwkMEQ8SEhEPERETFhwXExQaFRERGCEYGh0dHx8fExciJCIeJBweHx7/2wBDAQUFBQcGBw4ICA4eFBEUHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh4eHh7/wAARCAAEAAQDASIAAhEBAxEB/8QAFQABAQAAAAAAAAAAAAAAAAAAAAT/xAAaEAEAAQUAAAAAAAAAAAAAAAAABQIGNna0/8QAFAEBAAAAAAAAAAAAAAAAAAAAB//EAB4RAAAEBwAAAAAAAAAAAAAAAAABAzUCBQZygrGy/9oADAMBAAIRAxEAPwCuByC9tvl+qsAHTxxXuPYAa5fV8OIR/9k=")\n    chapter = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><link rel="stylesheet" href="../styles/book.css"/></head><body>
<h1>Illustrated chapter</h1><p class="image-caption">Before the image.</p>
<img src="../images/test.png" alt="Four colored squares"/>\n<img src="../images/sample.jpg" alt="JPEG sample"/>
<p class="boxed" style="margin-top: 18px; margin-bottom: 12px; margin-left: 15px; text-indent: 2em; page-break-after: always">After the image.</p>\n<ol><li>First item</li><li>Second item</li></ol>\n<p>Before break<br/>After break</p></body></html>"""
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
        z.writestr("OEBPS/images/sample.jpg", jpeg, compress_type=ZIP_DEFLATED)
        z.writestr("OEBPS/styles/book.css", "p.image-caption { text-align: center; font-style: italic; }", compress_type=ZIP_DEFLATED)


if __name__ == "__main__":
    main(sys.argv[1])
