#!/usr/bin/env python3
"""Small EPUB2/3 internal links and footnotes test book for Books."""
from pathlib import Path
import sys
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

def main(out):
    container='''<?xml version="1.0" encoding="utf-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
<rootfiles><rootfile full-path="OPS/package.opf" media-type="application/oebps-package+xml"/></rootfiles></container>'''
    opf='''<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier>urn:books:links:1</dc:identifier>
<dc:title>Books Links and Footnotes</dc:title><dc:language>en</dc:language>
</metadata>
<manifest>
<item id="one" href="text/first.xhtml" media-type="application/xhtml+xml"/>
<item id="two" href="text/second.xhtml" media-type="application/xhtml+xml"/>
</manifest><spine><itemref idref="one"/><itemref idref="two"/></spine></package>'''
    first='''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>First</title></head>
<body><h1>Chapter One</h1>
<p>This chapter has a <a href="#same">local internal link</a>,
a <a href="second.xhtml#target">second chapter link</a>, and
a <a epub:type="noteref" xmlns:epub="http://www.idpf.org/2007/ops"
 href="second.xhtml#footnote-1">footnote reference 1</a>.</p>
<p id="same">Destination on this page.</p></body></html>'''
    second='''<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Second</title></head>
<body><h1>Chapter Two</h1>
<p id="target">Second chapter destination</p>
<aside id="footnote-1" epub:type="footnote" xmlns:epub="http://www.idpf.org/2007/ops">
<p>This footnote is a short note on chapter one.</p></aside>
<p><a href="first.xhtml#same">Return to chapter one</a></p>
</body></html>'''
    Path(out).parent.mkdir(parents=True,exist_ok=True)
    with ZipFile(out,"w") as epub:
        epub.writestr("mimetype",b"application/epub+zip",compress_type=ZIP_STORED)
        for name,data in (
            ("META-INF/container.xml",container),
            ("OPS/package.opf",opf),
            ("OPS/text/first.xhtml",first),
            ("OPS/text/second.xhtml",second)):
            epub.writestr(name,data.encode("utf-8"),compress_type=ZIP_DEFLATED)
if __name__=="__main__":
    main(sys.argv[1])
