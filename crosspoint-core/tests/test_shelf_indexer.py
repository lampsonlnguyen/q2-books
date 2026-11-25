#!/usr/bin/env python3
"""Host integration test: unopened EPUB indexing, cache reuse/invalidation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

reader, maker = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="books-idle-index-") as tmp:
    book = Path(tmp)/"unopened.epub"
    state = Path(tmp)/"state"
    state.mkdir()
    subprocess.run([sys.executable,maker,str(book)],check=True)
    environment=dict(os.environ,BOOKS_STATE_DIR=str(state))
    def probe():
        return subprocess.run([reader,"--index-book",str(book)],
                              env=environment,text=True,capture_output=True,
                              check=True,timeout=30).stdout
    first=probe()
    assert "metadata=1 cover=1 updated=1" in first,first
    assert "Unopened Vietnamese Book" in first,first
    assert "Nguyễn Thị Mai" in first,first
    assert "Books color cover: ready" in first,first
    color_cache, = state.glob("**/*.flowcover")
    _, payload = color_cache.read_bytes().split(b"\n", 1)
    assert payload[:2] == b"\x08\x08", payload
    assert payload[2:4] == b"\x00\xf8" and payload[10:12] == b"\x1f\x00", payload
    # Older cards can still have the one-bit thumbnail with no RGB cache.
    color_cache.unlink()
    assert "Books color cover: ready" in probe()
    assert color_cache.exists()
    preview = Path(tmp)/"home.ppm"
    subprocess.run([reader,"--card-preview",tmp,str(preview)],env=environment,check=True,timeout=30)
    header = b"P6\n375 320\n255\n"
    frame = preview.read_bytes()
    assert frame.startswith(header)
    def pixel(x,y):
        at=len(header)+3*(y*375+x)
        return frame[at:at+3]
    assert pixel(28,110)==b"\xff\x00\x00" and pixel(57,110)==b"\x00\x00\xff"
    again=probe()
    assert "metadata=1 cover=1 updated=0" in again,again
    # File identity must invalidate metadata AND image caches for updates.
    old=book.stat()
    os.utime(book,ns=(old.st_atime_ns,old.st_mtime_ns+5_000_000_000))
    refreshed=probe()
    assert "metadata=1 cover=1 updated=1" in refreshed,refreshed
print("Books unopened EPUB OPF, cover cache and invalidation: PASS")
