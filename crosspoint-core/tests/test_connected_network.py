#!/usr/bin/env python3
"""End-to-end OPDS + Kavita/KOReader mock against host or Q2 MIPS binary.

Runs a disposable localhost server, tests chunked Atom, EPUB downloads,
KOReader partial-MD5 identity, and a real CrossPoint XPath progress roundtrip.
"""
from __future__ import annotations

import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import subprocess
import ssl
import sys
import tempfile
import threading
from zipfile import ZipFile

READER = Path(sys.argv[1]).resolve()
BOOK = Path(sys.argv[2]).resolve()
QEMU_ROOT = Path(sys.argv[3]).resolve() if len(sys.argv) > 3 else None
assert BOOK.is_file(), BOOK
book_data = BOOK.read_bytes()
assert book_data[:4] == b"PK\x03\x04"
synchronized = {}
received = []

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def respond(self, code, data, *, chunked=False, mime="application/json"):
        self.send_response(code)
        self.send_header("Content-Type", mime)
        self.send_header("Connection", "close")
        if chunked:
            self.send_header("Transfer-Encoding", "chunked")
        else:
            self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        if chunked:
            for start in range(0, len(data), 73):
                part = data[start:start + 73]
                self.wfile.write(f"{len(part):X}\r\n".encode() + part + b"\r\n")
            self.wfile.write(b"0\r\n\r\n")
        else:
            self.wfile.write(data)

    def authed(self):
        return (
            self.headers.get("x-auth-user") == "testreader"
            and self.headers.get("x-auth-key") == hashlib.md5(b"secret").hexdigest()
        )

    def do_GET(self):
        if self.path.startswith("/api/opds/redirect-"):
            location = ("/api/opds/key" if self.path.endswith("safe")
                        else "http://localhost:1/private" if self.path.endswith("downgrade")
                        else "https://example.org/private")
            self.send_response(302)
            self.send_header("Location", location)
            self.send_header("Content-Length", "0")
            self.send_header("Connection", "close")
            self.end_headers()
            return
        if self.path == "/api/opds/key":
            payload = b"""<?xml version="1.0"?>
<feed xmlns="http://www.w3.org/2005/Atom">
  <title>Test Kavita Library</title>
  <entry><title>Series Folder</title><link rel="subsection"
      type="application/atom+xml;profile=opds-catalog"
      href="/api/opds/key/series"/></entry>
  <entry><title>Books Links and Footnotes</title>
  <author><name>Mock Author</name></author><link
     rel="http://opds-spec.org/acquisition"
     type="application/epub+zip"
     href="/api/opds/key/download.epub"/></entry>
</feed>"""
            self.respond(200, payload, chunked=True, mime="application/atom+xml")
        elif self.path == "/api/opds/key/series":
            self.respond(200, b"""<feed xmlns="http://www.w3.org/2005/Atom">
              <title>Subcatalog</title><entry><title>Book</title>
              <link rel="http://opds-spec.org/acquisition"
              type="application/epub+zip"
              href="../download.epub"/></entry></feed>""",
              mime="application/atom+xml")
        elif self.path == "/api/opds/key/download.epub":
            self.respond(200, book_data, mime="application/epub+zip")
        elif self.path.startswith("/api/koreader/key/syncs/progress/"):
            if not self.authed():
                self.respond(401, b'{"error":"Unauthorized"}')
                return
            ident = self.path.split("/syncs/progress/", 1)[1].split("?")[0]
            self.respond(200, json.dumps(synchronized.get(ident, {})).encode())
        else:
            self.respond(404, b"{}")

    def do_PUT(self):
        if self.path != "/api/koreader/key/syncs/progress" or not self.authed():
            self.respond(401, b'{"error":"Unauthorized"}')
            return
        data = self.rfile.read(int(self.headers.get("Content-Length", "0")))
        payload = json.loads(data)
        assert payload["document"] and payload["progress"]
        assert payload["progress"].startswith("/body/DocFragment[")
        assert 0 <= payload["percentage"] <= 1
        synchronized[payload["document"]] = payload
        received.append(payload)
        self.respond(200, b"{}")

with tempfile.TemporaryDirectory(prefix="books-network-test-") as tmp:
    temp = Path(tmp)
    (temp / "Books").mkdir()
    (temp / "state").mkdir()
    if QEMU_ROOT:
        fonts = Path(os.environ.get("BOOKS_Q2_FONTS", ""))
        if fonts.is_dir():
            (temp / "state" / "fonts").symlink_to(fonts)
    listener = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    tls = os.environ.get("BOOKS_TEST_HTTPS") == "1"
    ca = temp / "local-ca.pem"
    if tls:
        key = temp / "local-ca.key"
        subprocess.run([
            "openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
            "-days", "1", "-sha256", "-subj", "/CN=localhost",
            "-addext", "subjectAltName=DNS:localhost",
            "-keyout", str(key), "-out", str(ca)
        ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        context.load_cert_chain(str(ca), str(key))
        listener.socket = context.wrap_socket(listener.socket, server_side=True)
    server = threading.Thread(target=listener.serve_forever, daemon=True)
    server.start()
    try:
        host, port = listener.server_address
        base = f"https://localhost:{port}" if tls else f"http://{host}:{port}"
        config = temp / "state" / "connected.conf"
        config.write_text(
            f"opds_url={base}/api/opds/key\n"
            f"sync_url={base}/api/koreader/key\n"
            "sync_username=testreader\nsync_password=secret\n"
            "enable_sync=1\n", encoding="utf-8"
        )
        env = dict(os.environ, BOOKS_STATE_DIR=str(temp / "state"))
        if tls:
            env["BOOKS_TEST_CA_BUNDLE"] = str(ca)
        prefix = (["qemu-mipsel", "-L", str(QEMU_ROOT), str(READER)]
                  if QEMU_ROOT else [str(READER)])
        def call(*args):
            result = subprocess.run(
                [*prefix, *args], capture_output=True, text=True,
                timeout=120, env=env
            )
            if result.returncode:
                raise AssertionError(
                    f"exit={result.returncode}\nstdout={result.stdout[-2000:]}"
                    f"\nstderr={result.stderr[-2000:]}"
                )
            return result.stdout
        if tls:
            # No CA trust and wrong hostname MUST fail. Never weaken TLS
            # verification to make a development server appear to work.
            untrusted = dict(env)
            untrusted.pop("BOOKS_TEST_CA_BUNDLE", None)
            attempt = subprocess.run(
                [*prefix, "--opds-probe", f"{base}/api/opds/key"],
                capture_output=True, text=True, timeout=30, env=untrusted)
            assert attempt.returncode != 0, "Untrusted certificate accepted"
            wrong_name = subprocess.run(
                [*prefix, "--opds-probe",
                 f"https://127.0.0.1:{port}/api/opds/key"],
                capture_output=True, text=True, timeout=30, env=env)
            assert wrong_name.returncode != 0, "Wrong TLS hostname accepted"
            safe = call("--opds-probe", f"{base}/api/opds/redirect-safe")
            assert "Test Kavita Library" in safe
            for kind in ("evil", "downgrade"):
                attempt = subprocess.run(
                    [*prefix, "--opds-probe", f"{base}/api/opds/redirect-{kind}"],
                    capture_output=True, text=True, timeout=30, env=env)
                assert attempt.returncode != 0, f"Unsafe {kind} redirect followed"
        output = call("--opds-probe", f"{base}/api/opds/key")
        assert "Test Kavita Library" in output, output
        assert "entries=2" in output, output
        output = call("--opds-probe", f"{base}/api/opds/key/series")
        assert "Subcatalog" in output, output
        output = call("--opds-download", f"{base}/api/opds/key",
                      str(temp / "Books"))
        assert "Books OPDS downloaded:" in output, output
        downloads = list((temp / "Books" / "Online").glob("*.epub"))
        assert len(downloads) == 1, downloads
        assert downloads[0].read_bytes() == book_data
        with ZipFile(downloads[0]) as epub:
            assert epub.testzip() is None
        identity = call("--koreader-hash", str(downloads[0]))
        digest = identity.strip().split()[-1]
        expected = hashlib.md5()
        size = len(book_data)
        for index in range(12):
            offset = 0 if index == 0 else 1024 << (2 * (index - 1))
            if offset < size:
                expected.update(book_data[offset:offset + 1024])
        assert digest == expected.hexdigest(), (digest, expected.hexdigest())
        output = call("--test-koreader-sync", str(downloads[0]))
        assert "Books KOReader XPath progress roundtrip: PASS" in output
        assert received and received[-1]["document"] == digest
        assert "DocFragment[" in received[-1]["progress"]
        if tls:
            print("Books HTTPS OPDS, download, partial-MD5 and KOSync: PASS")
        else:
            print("Books OPDS, download, partial-MD5 and KOSync: PASS")
    finally:
        listener.shutdown()
        listener.server_close()
        server.join(timeout=3)
