# SPDX-License-Identifier: MIT
"""Synthetic legacy MOBI and PalmDOC fixtures; no copyrighted ebook inputs."""
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zipfile
from test_markdown_reader import png


def make_book(text, mobi=True, compression=1, encryption=0, encoding=65001,
              version=6, payload=None, flags=0, exth=(), cover=None):
    if payload is None:
        if compression == 1:
            payload = text
        else:
            # PalmDOC literal runs, including bytes that otherwise encode LZ commands.
            payload = b"".join(bytes([len(text[i:i + 8])]) + text[i:i + 8]
                               for i in range(0, len(text), 8))
    first = struct.pack(">HHIHHHH", compression, 0, len(text), 1, 4096, encryption, 0)
    if mobi:
        mh = bytearray(0xe4)
        struct.pack_into(">4sIIIII", mh, 0, b"MOBI", len(mh), 2, encoding, 1, version)
        struct.pack_into(">H", mh, 0xe2, flags)
        title = b"Fixture ebook"
        entries = list(exth)
        if cover is not None:
            entries.append((201, struct.pack(">I", 0)))
            struct.pack_into(">I", mh, 92, 2)
        extension = b""
        if entries:
            records = b"".join(struct.pack(">II", kind, len(value) + 8) + value
                               for kind, value in entries)
            extension = struct.pack(">4sII", b"EXTH", 12 + len(records), len(entries)) + records
            struct.pack_into(">I", mh, 112, 0x40)
        struct.pack_into(">II", mh, 68, len(first) + len(mh) + len(extension), len(title))
        first += mh + extension + title
    pdb = bytearray(78)
    pdb[0:7] = b"Fixture"
    pdb[60:68] = b"BOOKMOBI" if mobi else b"TEXtREAd"
    records = [first, payload] + ([cover] if cover is not None else [])
    struct.pack_into(">H", pdb, 76, len(records))
    start = 78 + 8 * len(records) + 2
    table = b""
    for i, record in enumerate(records, 1):
        table += struct.pack(">IB3s", start, 0, i.to_bytes(3, "big"))
        start += len(record)
    return bytes(pdb) + table + b"\0\0" + b"".join(records)


def test(executable):
    with tempfile.TemporaryDirectory(prefix="books-mobi-") as temp:
        root = Path(temp)
        shelf = root / "Books"
        state = root / "state"
        shelf.mkdir()
        state.mkdir()

        def run(*args, ok=True):
            result = subprocess.run([executable, *map(str, args)],
                env={**os.environ, "BOOKS_STATE_DIR": str(state)},
                capture_output=True, text=True, timeout=30)
            assert (result.returncode == 0) == ok, result.stdout + result.stderr
            return result.stdout + result.stderr

        html = ("<html><body><h1>Opening</h1><p>Đà Lạt and 中文.</p>"
                "<p><b>Bold words</b> and <i>italic text</i>.</p>"
                + "<p>Reading progress fixture.</p>" * 300
                + "<p>Unique final sentence.</p></body></html>").encode()
        mobi = shelf / "Novel.MOBI"
        mobi.write_bytes(make_book(html, compression=2, cover=png((200, 30, 10)),
            exth=[(100, "Nguyễn Văn A".encode()), (101, b"Fixture Press"),
                  (503, "Đà Lạt Novel".encode()), (524, b"vi")]))
        prc = shelf / "Legacy.prc"
        prc.write_bytes(make_book(b"PalmDOC legacy text. " * 300, mobi=False))
        cp = shelf / "Western.mobi"
        cp.write_bytes(make_book(b"<html><body><p>Price \x80, caf\xe9.</p></body></html>",
                                 encoding=1252))
        lz = shelf / "Backref.prc"
        lz.write_bytes(make_book(b"abcabcabc A", mobi=False, compression=2,
                                 payload=b"abc\x80\x1b\xc1"))
        extra = shelf / "Trailing.mobi"
        extra_html = b"<p>Trailing record fixture.</p>"
        extra.write_bytes(make_book(extra_html, payload=extra_html+b"\x81", flags=2))

        # Metadata and artwork must be available before the reader converts text.
        indexed = run("--index-book", mobi)
        assert "metadata=1 cover=1 updated=1" in indexed, indexed
        assert "title=Đà Lạt Novel author=Nguyễn Văn A" in indexed, indexed
        assert "Books color cover: ready" in indexed, indexed
        assert not list((state / "cache").glob("mobi-v1-*.epub"))
        assert "updated=0" in run("--index-book", mobi)
        listing = run("--list-books", shelf)
        assert all(str(p) in listing for p in (mobi, prc, cp, lz, extra))
        assert "matches=1" in run("--test-book-search", mobi, "Đà Lạt")
        assert "matches=1" in run("--test-book-search", mobi, "Unique final sentence")
        assert "matches=1" in run("--test-book-search", cp, "café")
        assert "matches=1" in run("--test-book-search", lz, "abcabcabc")
        assert "matches=1" in run("--test-book-search", extra, "Trailing record fixture")
        run("--validate", prc)
        run("--test-reading-resume", mobi)
        run("--test-clippings", mobi)
        packages = list((state / "cache").glob("mobi-v1-*.epub"))
        assert len(packages) == 5, packages
        for path in packages:
            with zipfile.ZipFile(path) as z:
                assert z.testzip() is None
                assert "OEBPS/content.opf" in z.namelist()

        bad = shelf / "invalid.mobi"
        for data, diagnostic in [
            (make_book(html, encryption=2), "DRM-protected"),
            (make_book(html, version=8), "KF8-only"),
            (make_book(html, compression=17480, payload=b"abc"), "compression"),
            (make_book(b"abc", compression=2, payload=b"\x80\x08"), "PalmDOC"),
            (make_book(html)[:90], "record"),
            (make_book(html, payload=html+b"\x80", flags=2), "trailing"),
        ]:
            bad.write_bytes(data)
            assert diagnostic.lower() in run("--validate", bad, ok=False).lower()
        unrelated = shelf / "Application.prc"
        unrelated.write_bytes(b"\0" * 80)
        assert "not a supported" in run("--validate", unrelated, ok=False).lower()
        assert str(unrelated) not in run("--list-books", shelf)


if __name__ == "__main__":
    test(sys.argv[1])
