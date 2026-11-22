#!/usr/bin/env python3
"""Exercise the real Q2 reader's bounded streamed XHTML path.

A 3 MiB XML comment is intentionally ignored by Expat. It must not be retained
as an uncompressed chapter buffer; the visible text must still survive.
Corrupt XML and chapters beyond the 4 MiB limit must fail predictably.
"""
from pathlib import Path
import subprocess
import sys
import tempfile
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

reader, source = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="books-stream-") as td:
    root = Path(td)
    with ZipFile(source) as original:
        members = [(item.filename, original.read(item.filename),
                    item.compress_type) for item in original.infolist()]
    chapter_name = "OEBPS/text/chapter.xhtml"
    chapter = next(data for name, data, _ in members if name == chapter_name)
    marker = b"<body>"
    assert marker in chapter
    for mode, extra, should_pass in [
        ("large", b"<!--" + b"x" * (3 * 1024 * 1024) + b"-->", True),
        ("truncated", b"<unclosed>", False),
        ("too-large", b"<!--" + b"x" * (4 * 1024 * 1024) + b"-->", False),
    ]:
        target = root / (mode + ".epub")
        with ZipFile(target, "w") as z:
            for name, data, compression in members:
                if name == chapter_name:
                    data = data.replace(marker, marker + extra, 1)
                z.writestr(name, data, compress_type=compression)
        result = subprocess.run(
            [reader, "--inspect-styles", str(target), chapter_name],
            capture_output=True, text=True, timeout=45,
        )
        if should_pass:
            assert result.returncode == 0, (mode, result.stderr)
            assert "Before the image." in result.stdout
            assert "After nested" in result.stdout
            assert "Cell contents" in result.stdout
        else:
            assert result.returncode != 0, (mode, result.stdout)
print("Bounded CrossPoint ZIP-to-Expat XHTML streaming: PASS")
