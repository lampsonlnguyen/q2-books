#!/usr/bin/env python3
"""RD-04 TXT reading and RD-01/06 progress, end to end through books-reader.

Writes a ~300 KB UTF-8 .txt (about ten 32 KiB parts) and a Latin-1 .txt, then
runs --test-progress (go-to-percent at 0..100 %, monotonic book percentage,
focus re-layout keeping the place, auto-turn cycling) and --walk on each.
"""
import os
import subprocess
import sys
import tempfile

reader = sys.argv[1]
extra = sys.argv[2:]  # optional EPUBs to run --test-progress on too

def run(args, env):
    p = subprocess.run([reader] + args, env=env, capture_output=True, text=True, timeout=300)
    sys.stdout.write(p.stdout)
    if p.returncode != 0:
        sys.stderr.write(p.stderr[-4000:])
        sys.exit(f"FAIL {args[0]} {os.path.basename(args[-1])}: rc={p.returncode}")
    return p.stdout

with tempfile.TemporaryDirectory() as tmp:
    env = dict(os.environ, BOOKS_STATE_DIR=os.path.join(tmp, "state"))
    os.makedirs(env["BOOKS_STATE_DIR"])
    utf8 = os.path.join(tmp, "Long Story.txt")
    with open(utf8, "w", encoding="utf-8") as f:
        for i in range(3000):
            f.write(f"Paragraph {i}. Đây là một đoạn văn bản thuần dùng để kiểm tra "
                    "việc chia trang và phần trăm của cuốn sách.\n\n")
    latin = os.path.join(tmp, "legacy.txt")
    with open(latin, "wb") as f:
        f.write(("Caf\xe9 cr\xe8me, na\xefve fa\xe7ade.\n".encode("latin-1")) * 4000)
    # One word on the page: touch boxes must sit on the glyphs (selection
    # geometry treated the line top as the baseline: one line too high).
    one = os.path.join(tmp, "one.txt")
    for word in ["Hello", "Việt", "gypsy"]:
        with open(one, "w", encoding="utf-8") as f:
            f.write(word + "\n")
        run(["--test-selection-geometry", one], env)
    run(["--test-clippings", utf8], env)
    clips = os.path.join(env["BOOKS_STATE_DIR"], "Clippings", "Long Story.txt")
    assert os.path.isfile(clips), os.listdir(os.path.join(env["BOOKS_STATE_DIR"], "Clippings"))
    for book in [utf8, latin] + extra:
        run(["--test-progress", book], env)
        out = run(["--walk", book], env)
    print("TXT reading and go-to-percent: PASS")
