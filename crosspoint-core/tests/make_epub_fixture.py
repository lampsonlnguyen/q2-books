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
<dc:identifier id="bookid">books-multilingual-test</dc:identifier>
<dc:title>CrossPoint multilingual test</dc:title>\n<dc:creator>Books Test Author</dc:creator>
<dc:language>mul</dc:language></metadata>
<manifest><item id="c1" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
<item id="c2" href="chapter2.xhtml" media-type="application/xhtml+xml"/>
<item id="css" href="book.css" media-type="text/css"/></manifest>
<spine><itemref idref="c1"/><itemref idref="c2"/></spine>
</package>'''
chapter1 = '''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter 1</title><link rel="stylesheet" href="book.css"/></head>
<body><h1>Chapter One</h1><p>Nguyễn — Đà Lạt, 中文, 日本語, 한국어.</p>\n<p class="highlight"><strong>Bold text</strong> and <em>italic text</em> styled by book.css.</p>\n<p style="display:none">SECRET_HIDDEN_PARAGRAPH</p>
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
    z.writestr("OEBPS/book.css", "p { text-align: left; } p.highlight { text-align: center; font-style: italic; }", compress_type=ZIP_DEFLATED)
