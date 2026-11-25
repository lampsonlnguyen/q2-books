# SPDX-License-Identifier: MIT
from pathlib import Path
import os
import struct
import subprocess
import sys
import tempfile
from xml.etree import ElementTree
import zipfile
import zlib


def png(color):
    def chunk(key, data):
        return struct.pack(">I", len(data)) + key + data + struct.pack(">I", zlib.crc32(key + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 24, 24, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress((b"\0" + color * 24) * 24)) + chunk(b"IEND", b""))


def check():
    reader = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="books-html-") as tmp:
        root = Path(tmp)
        books = root / "Books"
        assets = books / "art"
        assets.mkdir(parents=True)
        state = root / "state"
        state.mkdir()
        image = assets / "local image.png"
        image.write_bytes(png(b"\xff\x00\x00"))
        outside = root / "outside.png"
        outside.write_bytes(png(b"\x00\x00\xff"))
        (assets / "symlink.png").symlink_to(outside)
        (books / "symlink-dir").symlink_to(root, target_is_directory=True)
        env = {**os.environ, "BOOKS_STATE_DIR": str(state)}

        def run(*args, success=True):
            result = subprocess.run([reader, *map(str, args)], env=env, text=True,
                                    capture_output=True, timeout=40)
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            return result.stdout

        html = books / "Local guide.HTML"
        html.write_text("""<!doctype html><HTML lang=vi><HEAD><title>Local &amp; illustrated</title>
        <style>@import url(https://example.invalid/global.css); .reading {text-align:center; background:url(bad)}</style>
        <body><main class=reading><h1>Opening &copy;</h1><p class=reading
        style='font-weight:bold;background:url(https://example.invalid/css.png)'>Hello <em>world</em>
        <a href="#later">Jump to destination</a> <a href="https://example.invalid/">External label</a>
        <p>Đà Lạt 中文 العربية <b>bold text</b>
        <ul><li>First<li>Second</ul>
        <img alt="local image" src="./art/local%20image.png" onclick="evil()">
        <p><img src="art/symlink.png" alt="linked file">
        <img src="symlink-dir/outside.png" alt="linked directory">
        <img src="%2e%2e/outside.png" alt="outside image">
        <img src="https://example.invalid/img.png" alt="remote image"></p>
        <script>script secret <img src="art/local%20image.png"></script>
        <iframe src="https://example.invalid/" >frame secret</iframe>
        <p hidden>hidden secret <span>content</span></p>
        """ + "<p>Long paragraph 中文 and more reading text.</p>" * 2100 + """
        <h2 id=later>Destination</h2><blockquote>Arrival text</blockquote>
        <p><a href="#spot">Spot link</a><span id=spot>Inline destination</span>
        <pre><code>if (a &lt; b)</code></pre></main>""", encoding="utf-8")
        western = books / "Western.htm"
        western.write_bytes(b'<meta charset="windows-1252"><p>caf\xe9 costs \x801</p>')
        wide = books / "Wide.htm"
        wide.write_bytes("<p>Unicode 😀 漢字</p>".encode("utf-16"))
        (books / "ignored.html.bak").touch()
        listing = run("--list-books", books)
        assert all(str(path) in listing for path in (html, western, wide))
        assert ".html.bak" not in listing
        toc = run("--inspect-toc", html)
        assert "Opening ©" in toc and "Destination" in toc
        assert "matches=1" in run("--test-book-search", html, "ĐÀ LẠT")
        assert "matches=1" in run("--test-book-search", html, "中文 العربية")
        assert "matches=1" in run("--test-book-search", html, "Arrival text")
        assert "matches=0" in run("--test-book-search", html, "secret")
        assert "matches=1" in run("--test-book-search", western, "café costs €1")
        assert "matches=1" in run("--test-book-search", wide, "😀 漢字")
        assert "Books rendered images: 1" in run("--render-images", html, root / "image.pgm")
        run("--test-reading-resume", html)
        run("--test-clippings", html)

        packages = list((state / "cache").glob("html-*.epub"))
        assert len(packages) == 3, packages
        for package in packages:
            with zipfile.ZipFile(package) as epub:
                if "OEBPS/book.css" not in epub.namelist():
                    continue
                chapters = {name: epub.read(name).decode() for name in epub.namelist() if name.endswith(".xhtml")}
                assert len(chapters) >= 3
                css = epub.read("OEBPS/book.css").decode()
                assert ".reading{text-align:center;}" in css and "url(" not in css
                for chapter in chapters.values():
                    ElementTree.fromstring(chapter)
                markup = "".join(chapters.values())
                assert all(item in markup for item in (
                    "linked file", "linked directory", "outside image", "remote image",
                    "<li>First</li><li>Second</li>", "<code>if (a &lt; b)</code>", "font-weight:bold;"))
                assert not any(item in markup for item in ("example.invalid", "secret", "onclick"))
                assert [name for name in epub.namelist() if name.endswith(".png")] == ["OEBPS/image0.png"]
                assert epub.read("OEBPS/image0.png") == image.read_bytes()
                destination = next(name for name, content in chapters.items() if "Arrival text" in content)
                assert "href=\"" + Path(destination).name + "#a" in chapters["OEBPS/h0.xhtml"]
                break
        else:
            raise AssertionError("HTML fixture package missing")

        image.write_bytes(png(b"\x00\xff\x00"))
        run("--inspect-toc", html)
        assert len(list((state / "cache").glob("html-*.epub"))) == 4
        too_large = books / "Too large.htm"
        too_large.write_bytes(b"x" * (1024 * 1024 + 1))
        run("--inspect-toc", too_large, success=False)


if __name__ == "__main__":
    check()
