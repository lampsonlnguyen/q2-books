#!/usr/bin/env python3
"""Small unopened-book fixture proving OPF-only title/author/cover indexing."""
from pathlib import Path
import struct
import sys
import zlib
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

def chunk(kind: bytes, value: bytes) -> bytes:
    return (struct.pack(">I",len(value))+kind+value+
            struct.pack(">I",zlib.crc32(kind+value)&0xffffffff))
def cover_png() -> bytes:
    rows=(b"\x00"+b"\xff\x00\x00"*4+b"\x00\x00\xff"*4)*8
    return (b"\x89PNG\r\n\x1a\n"+
            chunk(b"IHDR",struct.pack(">IIBBBBB",8,8,8,2,0,0,0))+
            chunk(b"IDAT",zlib.compress(rows))+
            chunk(b"IEND",b""))
def main(path: str):
    container='''<?xml version="1.0"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
<rootfiles><rootfile full-path="OEBPS/package.opf"
 media-type="application/oebps-package+xml"/></rootfiles></container>'''
    opf='''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier>books-index-cover-fixture</dc:identifier>
<dc:title>Unopened Vietnamese Book</dc:title>
<dc:creator>Nguyễn Thị Mai</dc:creator><dc:language>vi</dc:language>
</metadata>
<manifest><item id="cover" href="images/cover.png" media-type="image/png"
 properties="cover-image"/>
<item id="chapter" href="chapter.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine><itemref idref="chapter"/></spine></package>'''
    chapter='''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head>
<title>Chapter</title></head><body><p>Xin chào.</p></body></html>'''
    Path(path).parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(path,"w") as book:
        book.writestr("mimetype","application/epub+zip",compress_type=ZIP_STORED)
        book.writestr("META-INF/container.xml",container,compress_type=ZIP_DEFLATED)
        book.writestr("OEBPS/package.opf",opf,compress_type=ZIP_DEFLATED)
        book.writestr("OEBPS/images/cover.png",cover_png(),compress_type=ZIP_DEFLATED)
        book.writestr("OEBPS/chapter.xhtml",chapter,compress_type=ZIP_DEFLATED)
if __name__=="__main__":
    main(sys.argv[1])
