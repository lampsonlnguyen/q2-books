#!/usr/bin/env python3
"""Produce a deterministic multilingual EPUB fixture for CrossPoint ZIP/OPF tests."""
import sys
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

path = sys.argv[1]
container = '''<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
 <rootfiles><rootfile full-path="OEBPS/content.opf"
 media-type="application/oebps-package+xml"/></rootfiles>
</container>'''
opf = '''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="bookid">crosspod-multilingual-test</dc:identifier>
<dc:title>CrossPoint multilingual test</dc:title>
<dc:language>mul</dc:language></metadata>
<manifest><item id="c1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
<item id="c2" href="chapter2.xhtml" media-type="application/xhtml+xml"/></manifest>
<spine><itemref idref="c1"/><itemref idref="c2"/></spine>
</package>'''
chapter1 = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter 1</title></head>
<body><h1>Chapter One</h1><p>Nguyễn — Đà Lạt, 中文, 日本語, 한국어.</p>
<p dir="rtl">مرحبا بالعالم — שלום עולם</p></body></html>'''
chapter2 = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter 2</title></head>
<body><p>हिन्दी, ภาษาไทย, Ελληνικά, Français, русский язык.</p></body></html>'''
with ZipFile(path, "w") as z:
    z.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
    z.writestr("META-INF/container.xml", container, compress_type=ZIP_DEFLATED)
    z.writestr("OEBPS/content.opf", opf, compress_type=ZIP_DEFLATED)
    z.writestr("OEBPS/chapter1.xhtml", chapter1, compress_type=ZIP_DEFLATED)
    z.writestr("OEBPS/chapter2.xhtml", chapter2, compress_type=ZIP_DEFLATED)
