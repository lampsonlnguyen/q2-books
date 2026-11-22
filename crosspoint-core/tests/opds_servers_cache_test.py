#!/usr/bin/env python3
"""M4: a saved server's HTTP Basic credentials, the offline catalog copy and
one retry of an unreachable or failing server, against a local server.

  /private      needs Basic lam:pw (401 otherwise)
  /flaky        503 on every odd request, the feed on even ones
  /gone         404
"""
import base64
import os
import subprocess
import sys
import tempfile
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

READER = sys.argv[1]
hits = {}
auths = []


def feed(title, n):
    rows = "".join(f'<entry><title>{title} {i}</title><id>{i}</id><link rel="http://opds-spec.org/acquisition" '
                   f'type="application/epub+zip" href="/b/{i}.epub"/></entry>' for i in range(n))
    return (f'<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><title>{title}</title>{rows}</feed>').encode()


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def reply(self, code, body=b""):
        self.send_response(code)
        self.send_header("Content-Type", "application/atom+xml")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        hits[self.path] = hits.get(self.path, 0) + 1
        auth = self.headers.get("Authorization")
        auths.append((self.path, auth))
        if self.path == "/private":
            if auth != "Basic " + base64.b64encode(b"lam:pw").decode():
                return self.reply(401)
            return self.reply(200, feed("Private", 3))
        if self.path == "/flaky":
            return self.reply(503) if hits[self.path] % 2 else self.reply(200, feed("Flaky", 2))
        if self.path == "/open":
            return self.reply(200, feed("Open", 4))
        self.reply(404)


server = ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{server.server_port}"
state = tempfile.mkdtemp(prefix="books-servers-")
env = dict(os.environ, BOOKS_STATE_DIR=state)


def probe(url, expect_rc=0):
    p = subprocess.run([READER, "--opds-probe", url], capture_output=True, text=True, timeout=60, env=env)
    if p.returncode != expect_rc:
        sys.exit(f"FAIL probe {url}: rc={p.returncode}\n{p.stdout}{p.stderr}")
    return p.stdout + p.stderr


# No saved server: no credentials, so the server refuses (and is not retried).
out = probe(base + "/private", 8)
assert "401" in out and hits["/private"] == 1, (out, hits)
open(os.path.join(state, "connected.conf"), "w").write(
    f"[server]\nname=Mine\nopds_url={base}/private\nopds_username=lam\nopds_password=pw\n")
out = probe(base + "/private")
assert "entries=3" in out and "offline" not in out, out
assert auths[-1][1] and all(a is None for p, a in auths if p == "/open"), auths
# Another origin never gets them.
subprocess.run([READER, "--opds-probe", base.replace("127.0.0.1", "localhost") + "/open"],
               capture_output=True, timeout=60, env=env)  # localhost may resolve to ::1 only
assert all(a is None for p, a in auths if p == "/open"), auths
# A 503 is tried once more, after a pause.
start = time.time()
out = probe(base + "/flaky")
assert "entries=2" in out and hits["/flaky"] == 2 and time.time() - start >= 1.0, (out, hits)
# A 404 is the server's answer: no retry, no offline copy.
probe(base + "/gone", 8)
assert hits["/gone"] == 1
cache = os.path.join(state, "opds-cache")
files = os.listdir(cache)
assert len(files) in (2, 3), files  # private, flaky (and open, if localhost reached it)
assert all(os.stat(os.path.join(cache, f)).st_mode & 0o777 == 0o600 for f in files)
# The server goes away: the kept copy is shown, marked offline.
server.shutdown()
server.server_close()
out = probe(base + "/private")
assert "entries=3 (offline copy)" in out, out
probe(base + "/never-seen", 8)
# A copy whose first line names another URL (a hash collision) is not used.
for f in files:
    path = os.path.join(cache, f)
    data = open(path, "rb").read()
    if b"/private" in data.split(b"\n", 1)[0]:
        open(path, "wb").write(data.replace(b"/private\n", b"/elsewhere\n", 1))
probe(base + "/private", 8)
# The cache keeps at most 64 pages.
for i in range(70):
    open(os.path.join(cache, f"{i:016x}.xml"), "w").write("BOOKS-OPDS1 x\n<feed/>")
    os.utime(os.path.join(cache, f"{i:016x}.xml"), (1000 + i, 1000 + i))
srv2 = ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=srv2.serve_forever, daemon=True).start()
probe(f"http://127.0.0.1:{srv2.server_port}/open")
srv2.shutdown()
left = sorted(os.listdir(cache))
assert len(left) == 64 and "0000000000000000.xml" not in left, len(left)
print("OPDS servers, offline copy and retry: PASS")
