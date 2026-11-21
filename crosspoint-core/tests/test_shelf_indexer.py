#!/usr/bin/env python3
"""Host integration test: unopened EPUB indexing, cache reuse/invalidation."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

reader, maker = sys.argv[1:]
with tempfile.TemporaryDirectory(prefix="crosspod-idle-index-") as tmp:
    book = Path(tmp)/"unopened.epub"
    state = Path(tmp)/"state"
    state.mkdir()
    subprocess.run([sys.executable,maker,str(book)],check=True)
    environment=dict(os.environ,CROSSPOD_STATE_DIR=str(state))
    def probe():
        return subprocess.run([reader,"--index-book",str(book)],
                              env=environment,text=True,capture_output=True,
                              check=True,timeout=30).stdout
    first=probe()
    assert "metadata=1 cover=1 updated=1" in first,first
    assert "Unopened Vietnamese Book" in first,first
    assert "Nguyễn Thị Mai" in first,first
    again=probe()
    assert "metadata=1 cover=1 updated=0" in again,again
    # File identity must invalidate metadata AND image caches for updates.
    old=book.stat()
    os.utime(book,ns=(old.st_atime_ns,old.st_mtime_ns+5_000_000_000))
    refreshed=probe()
    assert "metadata=1 cover=1 updated=1" in refreshed,refreshed
print("CrossPod unopened EPUB OPF, cover cache and invalidation: PASS")
