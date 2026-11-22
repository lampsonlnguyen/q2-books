#!/usr/bin/env python3
"""M4 OPDS rel="next" paging and OpenSearch search against a local server.

Pages: /opds?page=1..3 (rel=next chains them, page 3 is last), a looping
feed (/loop next -> itself), and search through an OpenSearch description
(/osd.xml -> /find?q={searchTerms}&page={startPage?}).
"""
import subprocess
import sys
import threading
import urllib.parse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

READER = sys.argv[1]
TITLES = [f"Book {i:02d}" for i in range(1, 26)]


def feed(title, entries, links=""):
    rows = "".join(
        f'<entry><title>{t}</title><id>{t}</id><link rel="http://opds-spec.org/acquisition" '
        f'type="application/epub+zip" href="/b/{urllib.parse.quote(t)}.epub"/></entry>' for t in entries)
    return (f'<?xml version="1.0"?><feed xmlns="http://www.w3.org/2005/Atom"><title>{title}</title>'
            f'{links}{rows}</feed>').encode()


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def do_GET(self):
        u = urllib.parse.urlparse(self.path)
        q = urllib.parse.parse_qs(u.query)
        mime = "application/atom+xml"
        if u.path == "/opds":
            page = int(q.get("page", ["1"])[0])
            items = TITLES[(page - 1) * 10: page * 10]
            links = '<link rel="search" type="application/opensearchdescription+xml" href="/osd.xml"/>'
            if page < 3:
                links += f'<link rel="next" type="application/atom+xml" href="/opds?page={page + 1}"/>'
            body = feed(f"Page {page}", items, links)
        elif u.path == "/loop":
            body = feed("Loop", ["Again"], '<link rel="next" href="/loop"/>')
        elif u.path == "/osd.xml":
            mime = "application/opensearchdescription+xml"
            body = (b'<?xml version="1.0"?><OpenSearchDescription xmlns="http://a9.com/-/spec/opensearch/1.1/">'
                    b'<Url type="application/atom+xml" template="/find?q={searchTerms}&amp;page={startPage?}"/>'
                    b'</OpenSearchDescription>')
        elif u.path == "/find":
            term = q.get("q", [""])[0]
            assert q.get("page") == ["1"], q
            body = feed(f"Results for {term}", [t for t in TITLES if term.lower() in t.lower()])
        else:
            self.send_response(404); self.send_header("Content-Length", "0"); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Type", mime)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)


server = ThreadingHTTPServer(("127.0.0.1", 0), H)
threading.Thread(target=server.serve_forever, daemon=True).start()
base = f"http://127.0.0.1:{server.server_port}"


def run(*args):
    p = subprocess.run([READER, *args], capture_output=True, text=True, timeout=60)
    print(p.stdout, end="")
    if p.returncode:
        print(p.stderr[-2000:])
        sys.exit(f"FAIL {args}: rc={p.returncode}")
    return p.stdout


out = run("--opds-pages", base + "/opds")
assert "total: 25 entries in 3 pages" in out, out
out = run("--opds-pages", base + "/loop")
assert "loops; stopped" in out and "in 1 pages" in out, out
out = run("--opds-search", base + "/opds", "Book 2")
assert "q=Book%202&page=1" in out, out
assert "total: 6 entries" in out, out  # Book 02 and Book 20-25
server.shutdown()
print("OPDS paging and search: PASS")
