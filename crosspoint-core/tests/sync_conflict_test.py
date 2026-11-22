#!/usr/bin/env python3
"""M4 KOReader sync conflicts, against a local KOReader sync server.

Push when the server is further on (another device read ahead): kept as a
conflict, not overwritten, until the push is forced. Pull when the server
is behind this Q2's page: kept as a conflict until forced; a forced pull
goes back to the server's position. A small gap (under 0.2 %) is no conflict.
"""
import json
import os
import subprocess
import sys
import tempfile
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

READER, EPUB = sys.argv[1], sys.argv[2]
store = {}
puts = []


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, body=b"{}"):
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        doc = self.path.split("/syncs/progress/", 1)[1].split("?")[0]
        self.reply(200, json.dumps(store.get(doc, {})).encode())

    def do_PUT(self):
        payload = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        puts.append(payload)
        store[payload["document"]] = payload
        self.reply(200, b'{"document":"x","timestamp":1}')


server = ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=server.serve_forever, daemon=True).start()
state = tempfile.mkdtemp(prefix="books-sync-")
open(os.path.join(state, "connected.conf"), "w").write(
    f"sync_url=http://127.0.0.1:{server.server_port}/sync\nsync_username=u\nsync_password=p\nenable_sync=true\n")
env = dict(os.environ, BOOKS_STATE_DIR=state)


def run(*args):
    p = subprocess.run([READER, "--test-sync", EPUB, *args], capture_output=True, text=True, timeout=120, env=env)
    if p.returncode not in (0, 7):
        sys.exit(f"FAIL {args}: rc={p.returncode}\n{p.stdout}{p.stderr[-1500:]}")
    return p.stdout.strip().splitlines()[-1]


out = run("0", "push")
assert out.startswith("ok Progress uploaded"), out
doc, = store
first = dict(store[doc])
# Another device reads further.
store[doc] = dict(first, percentage=min(1.0, first["percentage"] + 0.5), progress=first["progress"] + "/text().9", device="Kobo Libra")
out = run("0", "push")
assert out.startswith("conflict ahead") and "device=Kobo Libra" in out, out
assert len(puts) == 1  # not overwritten
out = run("0", "push", "force")
assert out.startswith("ok") and len(puts) == 2 and store[doc]["percentage"] == first["percentage"], out
# A tiny gap is the same place: no conflict.
store[doc] = dict(first, percentage=first["percentage"] + 0.001, progress=first["progress"] + "x")
out = run("0", "push")
assert out.startswith("ok"), out
# This Q2 reads further (chapter 2); the server still has chapter 1: behind.
store[doc] = dict(first, device="Kobo Libra")
out = run("1", "pull")
assert out.startswith("conflict behind"), out
out = run("1", "pull", "force")  # go back to the server's position
assert out.startswith("ok Remote position applied"), out
assert int(out.split("percent=")[1]) <= round(first["percentage"] * 100) + 1, (out, first["percentage"])
# A server further on is simply applied by a pull (no question).
store[doc] = dict(puts[-1])
pulled = run("0", "pull")
assert pulled.startswith("ok Remote position applied"), pulled
print("KOReader sync conflicts: PASS")
