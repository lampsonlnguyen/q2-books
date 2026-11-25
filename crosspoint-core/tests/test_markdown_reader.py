# SPDX-License-Identifier: MIT
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET
import zipfile
import zlib


def png(color):
    def chunk(kind, data):
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data)))

    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 24, 24, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress((b"\0" + bytes(color) * 24) * 24)) +
            chunk(b"IEND", b""))


def test(executable):
    with tempfile.TemporaryDirectory(prefix="books-md-") as temp:
        root = Path(temp)
        books = root / "Books"
        books.mkdir()
        assets = books / "art"
        assets.mkdir()
        picture = assets / "an image.png"
        picture.write_bytes(png((255, 20, 10)))
        (root / "outside.png").write_bytes(png((10, 20, 255)))
        (assets / "symlink.png").symlink_to(root / "outside.png")
        (books / "outside-dir").symlink_to(root, target_is_directory=True)
        state = root / "state"
        state.mkdir()

        def run(*args, success=True):
            env = {**os.environ, "BOOKS_STATE_DIR": str(state)}
            result = subprocess.run([executable, *map(str, args)], env=env, text=True,
                                    capture_output=True, timeout=30)
            assert (result.returncode == 0) == success, result.stdout + result.stderr
            return result.stdout

        md = books / "Đà Lạt & More.MD"
        md.write_bytes(b"\xef\xbb\xbf" + (
            "# Opening\n\nFirst words with **bold** and *emphasis* &copy;.\n\n"
            "[Go to the second section](#second-section) and [external](https://example.com).\n\n"
            "![Local art](art/an%20image.png)\n\n"
            "![linked file](art/symlink.png)\n\n"
            "![linked folder](outside-dir/outside.png)\n\n"
            "![outside](%2e%2e/outside.png)\n\n"
            "![remote](https://example.com/remote.png)\n\n"
            "## Second section\n\nĐà Lạt 中文 العربية\n\n"
            "> A quote with \x01 malformed text.\n\n"
            "3. Third item\n4. Fourth item\n\n"
            "\x60\x60\x60c\nif (x < y) {\n    work();\n}\n\x60\x60\x60\n\n"
            "<script>literal text</script>\n\n"
            "### Duplicate title\n\n### Duplicate title\n\n[Next](#duplicate-title-1)\n\n"
            + "Multilingual paragraph 中文.\n\n" * 2300 + "Unique final sentence.\n"
        ).encode("utf-8") + b"\xff")
        (books / "ignored.md.bak").touch()

        listing = run("--list-books", books)
        assert str(md) in listing and ".md.bak" not in listing
        toc = run("--inspect-toc", md)
        assert "title=Opening anchor=h0" in toc, toc
        assert "title=Second section anchor=h1" in toc, toc
        assert toc.count("title=Duplicate title") == 2, toc
        assert "matches=1" in run("--test-book-search", md, "ĐÀ LẠT")
        assert "matches=1" in run("--test-book-search", md, "中文 العربية")
        assert "matches=1" in run("--test-book-search", md, "Unique final sentence")
        run("--test-reading-resume", md)
        run("--test-clippings", md)
        assert "Books rendered images: 1" in run("--render-images", md, root / "image.pgm")

        cache = state / "cache"
        packages = set(cache.glob("md-*.epub"))
        assert len(packages) == 1
        package = packages.pop()
        with zipfile.ZipFile(package) as epub:
            assert epub.testzip() is None
            assert [p for p in epub.namelist() if p.endswith(".png")] == ["OEBPS/image0.png"]
            assert epub.read("OEBPS/image0.png") == picture.read_bytes()
            chapters = [epub.read(p).decode() for p in epub.namelist() if p.endswith(".xhtml")]
            assert len(chapters) >= 3
            for chapter in chapters:
                ET.fromstring(chapter)
            assert 'href="m1.xhtml#h1"' in chapters[0]
            assert '<strong>bold</strong>' in chapters[0] and '<em>emphasis</em>' in chapters[0]
            assert '©' in chapters[0] and 'https://' not in chapters[0]
            assert all(alt in chapters[0] for alt in ("linked file", "linked folder", "outside", "remote"))
            assert '<blockquote>' in chapters[1] and '<ol start="3">' in chapters[1]
            assert '<pre><code>if (x &lt; y)' in chapters[1]
            assert '&lt;script&gt;literal text&lt;/script&gt;' in chapters[1]
            assert 'href="m1.xhtml#h3"' in chapters[1]
            assert '\ufffd' in chapters[1] and '\ufffd' in chapters[-1]

        picture.write_bytes(png((10, 100, 255)))
        run("--validate", md)
        changed = set(cache.glob("md-*.epub")) - {package}
        assert len(changed) == 1
        with zipfile.ZipFile(changed.pop()) as epub:
            assert epub.read("OEBPS/image0.png") == picture.read_bytes()

        oversized = books / "oversized.md"
        with oversized.open("wb") as handle:
            handle.truncate(1024 * 1024 + 1)
        run("--validate", oversized, success=False)


if __name__ == "__main__":
    test(sys.argv[1])
