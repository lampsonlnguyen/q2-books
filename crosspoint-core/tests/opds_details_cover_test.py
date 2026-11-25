#!/usr/bin/env python3
"""M4: a catalog book's description (HTML content made plain) and its OPDS
color thumbnail, fetched from the catalog's origin and kept on the card
for the next time (served once, then the server is gone)."""
import os
import struct
import subprocess
import sys
import tempfile
import threading
import zlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

READER = sys.argv[1]


def png(w, h):
    row = b"\0" + b"\xff\x00\x00" * (w // 2) + b"\x00\x00\xff" * (w - w // 2)
    raw = row * h

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


FEED = ('<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><title>Covers</title>'
        '<entry><title>Picture Book</title><author><name>An Author</name></author><id>1</id>'
        '<content type="html">&lt;p&gt;First &amp;amp; best&lt;/p&gt;&lt;br/&gt;  second&amp;nbsp;line '
        '&amp;hellip;&lt;b&gt;bold&lt;/b&gt;</content>'
        '<link rel="http://opds-spec.org/image" type="image/png" href="/full.png"/>'
        '<link rel="http://opds-spec.org/image/thumbnail" type="image/png" href="/thumb.png"/>'
        '<link rel="http://opds-spec.org/acquisition" type="application/epub+zip" href="/b.epub"/></entry>'
        '</feed>').encode()
hits = []


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        hits.append(self.path)
        body, mime = {"/feed": (FEED, "application/atom+xml"), "/thumb.png": (png(60, 80), "image/png")}.get(
            self.path, (None, None))
        if body is None:
            self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=server.serve_forever, daemon=True).start()
url = f"http://127.0.0.1:{server.server_port}/feed"
state = tempfile.mkdtemp(prefix="books-covers-")
env = dict(os.environ, BOOKS_STATE_DIR=state)
out_ppm = os.path.join(state, "cover.ppm")


def run():
    p = subprocess.run([READER, "--opds-cover", url, out_ppm], capture_output=True, text=True, timeout=60, env=env)
    if p.returncode:
        sys.exit(f"FAIL rc={p.returncode}\n{p.stdout}{p.stderr}")
    return p.stdout


out = run()
assert "summary: First & best second line …bold" in out, out
assert "thumbnail: " + url.replace("/feed", "/thumb.png") in out, out  # the thumbnail, not the full image
assert "/thumb.png" in hits and "/full.png" not in hits, hits
data = open(out_ppm, "rb").read()
assert data.startswith(b"P6\n96 128\n255\n"), data[:20]
pix = data[len(b"P6\n96 128\n255\n"):]
assert pix[(64 * 96 + 30) * 3: (64 * 96 + 30) * 3 + 3] == b"\xff\x00\x00"
assert pix[(64 * 96 + 70) * 3: (64 * 96 + 70) * 3 + 3] == b"\x00\x00\xff"
covers = os.listdir(os.path.join(state, "opds-covers"))
assert len(covers) == 1 and covers[0].endswith(".cover"), covers
# Second time: the cover comes from the card (the feed from the offline copy).
server.shutdown()
server.server_close()
before = len(hits)
out = run()
assert "cover: ok" in out and len(hits) == before, (out, hits)
assert open(out_ppm, "rb").read() == data
print("OPDS details and covers: PASS")
