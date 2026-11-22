#!/usr/bin/env python3
"""Integration gate for incremental pages, forward/back and benchmark output."""
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from zipfile import ZipFile

reader, book = sys.argv[1:]
def run(command, seconds=90, source=book, state=None):
    env = os.environ.copy()
    if state is not None:
        env["CROSSPOD_STATE_DIR"] = str(state)
    result=subprocess.run([reader,command,str(source)],capture_output=True,
                          text=True,timeout=seconds,env=env)
    assert result.returncode==0, (command,result.stdout,result.stderr)
    return result.stdout

# A cold open of a normal chapter should begin pagination without staging XHTML
# to disk. A chapter over the RAM limit still uses the on-disk streaming path.
with tempfile.TemporaryDirectory(prefix="crosspod-chapter-") as tmp:
    root = Path(tmp)
    opening = run("--validate", state=root)
    assert not list((root / "cache").glob("book-*/html/*.html"))
    large = root / "large.epub"
    with ZipFile(book) as original, ZipFile(large, "w") as expanded:
        for entry in original.infolist():
            data = original.read(entry)
            if entry.filename == "OEBPS/long.xhtml":
                data = data.replace(b"<body>", b"<body><!--" + b"x" * (1024 * 1024) + b"-->", 1)
                assert len(data) > 1024 * 1024
            expanded.writestr(entry, data)
    assert "CrossPod original Section:" in run("--validate", source=large, state=root)
    assert len(list((root / "cache").glob("book-*/html/0.html"))) == 1
assert "CrossPod original Section:" in opening, opening
assert "(incremental)" in opening, "First open must not paginate whole chapter"

walking=run("--walk",120)
match=re.search(r"CrossPod walked (\d+) pages forward and (\d+) backward",
                walking)
assert match, walking
forward,backward=map(int,match.groups())
assert forward>=10, f"Long chapter yielded only {forward} page turns"
assert forward==backward, (forward,backward)

raster=run("--render-walk",120)
assert f"CrossPod rendered {forward} pages forward and {backward} backward" in raster, raster

stats=run("--bench",120)
match=re.search(
    r"open_ms=(\d+) first_render_us=(\d+) cached_render_us=(\d+) "
    r"sample_pages=(\d+) walk_ms=(\d+) maxrss_kb=(\d+)",
    stats)
assert match, stats
start_ms,first_us,cached_us,pages,walk_ms,rss=map(int,match.groups())
assert pages>=10 and rss>0 and first_us>=0 and cached_us>=0
print(f"Original Section incremental: PASS {forward} pages, "
      f"startup={start_ms}ms, first={first_us}us, cached={cached_us}us, "
      f"sample={pages}, walk={walk_ms}ms, "
      f"peak RSS={rss}KB")
