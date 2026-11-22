#!/usr/bin/env python3
"""OPDS EPUB downloads across interruption: SIGTERM stops a transfer at once and
leaves no .part; a .part left by a killed reader is taken over, not a permanent
"in progress"; one a live download holds is not; a slow but steady download is
not cut off by a total time limit.

Usage: download_lifecycle_test.py READER BOOK.epub  (BOOKS_TEST_HTTPS=1: over TLS)
"""
import fcntl
import os
import signal
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

READER = Path(sys.argv[1]).resolve()
BOOK = Path(sys.argv[2]).read_bytes()
TLS = os.environ.get("BOOKS_TEST_HTTPS") == "1"
pace = {"bytes_per_second": 0}  # 0: all at once


class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def do_GET(self):
        if self.path.endswith(".epub"):
            self.send_response(200)
            self.send_header("Content-Type", "application/epub+zip")
            self.send_header("Content-Length", str(len(BOOK)))
            self.end_headers()
            rate = pace["bytes_per_second"]
            try:
                if not rate:
                    self.wfile.write(BOOK)
                    return
                step = max(1, rate // 10)
                for start in range(0, len(BOOK), step):
                    self.wfile.write(BOOK[start:start + step])
                    self.wfile.flush()
                    time.sleep(0.1)
            except (BrokenPipeError, ConnectionResetError, ssl.SSLError):
                pass
            return
        body = (b'<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><title>T</title>'
                b'<entry><title>Lifecycle Book</title><author><name>A</name></author>'
                b'<link rel="http://opds-spec.org/acquisition" type="application/epub+zip" '
                b'href="/api/opds/key/book.epub"/></entry></feed>')
        self.send_response(200)
        self.send_header("Content-Type", "application/atom+xml")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


with tempfile.TemporaryDirectory(prefix="books-download-") as tmp:
    temp = Path(tmp)
    books = temp / "Books"
    books.mkdir()
    state = temp / "state"
    state.mkdir()
    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    env = dict(os.environ, BOOKS_STATE_DIR=str(state))
    if TLS:
        key, cert = temp / "key.pem", temp / "cert.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes", "-days", "1",
                        "-sha256", "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost",
                        "-keyout", str(key), "-out", str(cert)],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(cert), str(key))
        server.socket = context.wrap_socket(server.socket, server_side=True)
        env["BOOKS_TEST_CA_BUNDLE"] = str(cert)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    feed = f"{'https://localhost' if TLS else 'http://127.0.0.1'}:{server.server_address[1]}/api/opds/key"
    online = books / "Online"

    def download(timeout=120):
        return subprocess.run([str(READER), "--opds-download", feed, str(books)],
                              capture_output=True, text=True, timeout=timeout, env=env)

    def parts():
        return sorted(online.glob("*.part")) if online.exists() else []

    def epubs():
        return sorted(online.glob("*.epub")) if online.exists() else []

    # 1. SIGTERM mid-transfer: out at once, nothing left behind.
    pace["bytes_per_second"] = 2000
    reader = subprocess.Popen([str(READER), "--opds-download", feed, str(books)],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=env)
    for _ in range(100):
        if parts():
            break
        time.sleep(0.05)
    assert parts(), "download never started"
    started = time.monotonic()
    reader.send_signal(signal.SIGTERM)
    reader.wait(10)
    took = time.monotonic() - started
    assert took < 1.5, f"SIGTERM took {took:.1f}s to stop the transfer (Q2 Pod kills at 3 s)"
    assert reader.returncode != 0
    assert not parts() and not epubs(), (parts(), epubs())

    # 2. A .part a SIGKILLed reader left (no lock held) is taken over.
    pace["bytes_per_second"] = 2000
    reader = subprocess.Popen([str(READER), "--opds-download", feed, str(books)],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    for _ in range(100):
        if parts():
            break
        time.sleep(0.05)
    reader.kill()
    reader.wait()
    assert parts(), "the kill should leave a .part, as on a Q2"
    pace["bytes_per_second"] = 0
    result = download()
    assert result.returncode == 0, (result.stdout, result.stderr)
    assert len(epubs()) == 1 and epubs()[0].read_bytes() == BOOK
    assert not parts()
    epubs()[0].unlink()

    # 3. A .part a running download holds (locked) is not taken over.
    result = download()  # learn the destination name
    target = epubs()[0]
    target.unlink()
    part = Path(str(target) + ".part")
    with open(part, "wb") as handle:
        handle.write(b"partial")
        handle.flush()
        fcntl.flock(handle, fcntl.LOCK_EX)
        result = download()
        assert result.returncode != 0 and "in progress" in result.stderr, result.stderr
        assert part.read_bytes() == b"partial", "a live download's .part was touched"
    part.unlink()

    # 4. Slow but steady: no total time limit cuts it off (the old HTTP one was 20 s).
    pace["bytes_per_second"] = max(1, len(BOOK) // 25)
    started = time.monotonic()
    result = download(timeout=180)
    took = time.monotonic() - started
    assert result.returncode == 0, (took, result.stdout, result.stderr)
    assert took > 20, f"the steady download should take ~25 s, took {took:.1f}s"
    assert len(epubs()) == 1 and epubs()[0].read_bytes() == BOOK
    server.shutdown()
print(f"OPDS download lifecycle over {'HTTPS' if TLS else 'HTTP'}: PASS")
