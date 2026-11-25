# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
import zipfile


def book(path, series, font=None, src="../fonts/book.ttf", repeated=0):
    with zipfile.ZipFile(path, "w") as epub:
        epub.writestr("mimetype", "application/epub+zip")
        epub.writestr("META-INF/container.xml", """<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container"
          version="1.0"><rootfiles><rootfile full-path="OPS/content.opf"
          media-type="application/oebps-package+xml"/></rootfiles></container>""")
        epub.writestr("OPS/content.opf", f"""<package xmlns="http://www.idpf.org/2007/opf"
          xmlns:dc="http://purl.org/dc/elements/1.1/" unique-identifier="id" version="3.0">
          <metadata><dc:title>Đà Lạt</dc:title><dc:creator>Author</dc:creator>
          <dc:language>en-US</dc:language><dc:identifier id="id">urn:fixture:book</dc:identifier>
          <dc:publisher>Q2 Books</dc:publisher><dc:description>A &amp; B &lt;b&gt;plain&lt;/b&gt;</dc:description>
          {series}</metadata><manifest>
          <item id="a" href="a.xhtml" media-type="application/xhtml+xml"/>
          <item id="b" href="b.xhtml" media-type="application/xhtml+xml"/>
          <item id="style" href="css/book.css" media-type="text/css"/>
          <item id="font" href="fonts/book.ttf" media-type="font/ttf"/>
          </manifest><spine><itemref idref="a"/><itemref idref="b"/></spine></package>""")
        epub.writestr("OPS/css/book.css",
                      f'@font-face {{font-family:"Book";src:url("{src}")}} body {{font-family: Book, serif}}')
        if font:
            epub.writestr("OPS/fonts/book.ttf", font)
        epub.writestr("OPS/a.xhtml", """<html xmlns="http://www.w3.org/1999/xhtml"><head>
          <title>hidden match</title><link href="css/book.css" rel="stylesheet" type="text/css"/>
          </head><body><p>Opening words with <em>bold</em> and ordinary text.</p>
          <p xml:lang="fr">extraordinaire</p></body></html>""")
        many = " needle" * repeated
        epub.writestr("OPS/b.xhtml", f"""<html xmlns="http://www.w3.org/1999/xhtml"><body>
          <p>Đà <b>Lạt</b> 字形 العربية</p><p>another Đà Lạt occurrence</p>
          <p hidden="hidden">hidden match</p><p>{many}</p></body></html>""")


def restrict_embedding(data):
    changed = bytearray(data)
    tables = struct.unpack_from(">H", data, 4)[0]
    for i in range(tables):
        tag, _, offset, length = struct.unpack_from(">4sIII", data, 12 + i * 16)
        if tag == b"OS/2" and length >= 10:
            struct.pack_into(">H", changed, offset + 8, 2)
            return changed
    raise AssertionError("font has no OS/2 table")


def check():
    executable = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="books-roadmap-") as temp:
        root = Path(temp)
        env = {**os.environ, "BOOKS_STATE_DIR": temp}

        def command(*args):
            return subprocess.check_output([executable, *map(str, args)], env=env, text=True)

        epub = root / "second.epub"
        series = """<meta property="belongs-to-collection" id="series">Lạt &amp; Friends</meta>
          <meta property="collection-type" refines="#series">series</meta>
          <meta property="group-position" refines="#series">2.5</meta>"""
        book(epub, series)
        result = command("--test-local-book-info", epub)
        for expected in ("title=Đà Lạt", "publisher=Q2 Books", "lang=en-US", "series=Lạt & Friends",
                         "volume=2.5", "identifier=urn:fixture:book", "description=A & B <b>plain</b>"):
            assert expected in result, result
        assert "matches=2" in command("--test-book-search", epub, "ĐÀ LẠT")
        assert "matches=1" in command("--test-book-search", epub, "字形 العربية")
        assert "matches=0" in command("--test-book-search", epub, "hidden match")

        # TXT is wrapped into the same cached EPUB spine; test actual search,
        # Unicode normalization and the CLI's jump/restore anchor assertions.
        txt = root / "search.txt"
        txt.write_text("Opening paragraph.\n\n" + "Đà Lạt 字形 العربية\n" * 3 +
                       "needle " * 270, encoding="utf-8")
        assert "matches=3" in command("--test-book-search", txt, "ĐÀ LẠT")
        assert "matches=3" in command("--test-book-search", txt, "字形 العربية")
        assert "matches=270" in command("--test-book-search", txt, "needle")
        assert "matches=0" in command("--test-book-search", txt, "not in book")
        multi = root / "multi.txt"
        multi.write_text("Paragraph filler.\n\n" * 3000 + "unique ending", encoding="utf-8")
        assert "matches=1" in command("--test-book-search", multi, "unique ending")
        latin = root / "legacy.txt"
        latin.write_bytes("Café crème.\n".encode("latin-1"))
        assert "matches=1" in command("--test-book-search", latin, "CAFÉ")

        legacy = root / "first.epub"
        book(legacy, '<meta name="calibre:series" content="Lạt &amp; Friends"/>'
             '<meta name="calibre:series_index" content="1"/>')
        assert "volume=1" in command("--test-local-book-info", legacy)
        command("--test-library-roadmap", epub, legacy)
        book(legacy, series, repeated=270)
        assert "matches=270" in command("--test-book-search", legacy, "needle")

        if len(sys.argv) < 3:
            return
        font = Path(sys.argv[2]).read_bytes()
        rendered = root / "rendered.pgm"
        plain = root / "plain.pgm"
        book(epub, series, font)
        assert "publisher-font=1" in command("--test-book-font", epub, rendered)
        book(epub, series, restrict_embedding(font))
        assert "publisher-font=0" in command("--test-book-font", epub, plain)
        assert rendered.read_bytes().split(b"\n", 3)[3][:375 * 230] != plain.read_bytes().split(b"\n", 3)[3][:375 * 230]
        book(epub, series, font, "%2e%2e/%2e%2e/%2e%2e/OPS/fonts/book.ttf")
        assert "publisher-font=0" in command("--test-book-font", epub, plain)


if __name__ == "__main__":
    check()
