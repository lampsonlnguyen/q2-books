#!/usr/bin/env python3
"""Host shell launcher test, including logs for exec failures."""
import os
import pathlib
import subprocess
import sys
import tempfile

script = pathlib.Path(sys.argv[1]).resolve()
with tempfile.TemporaryDirectory(prefix="books-boot-") as root:
    app = pathlib.Path(root)
    engine = app / "books-engine"
    # Host ELF stand-in: launcher's success path must preserve exit status.
    engine.write_text("#!/bin/sh\necho books-engine-running\nexit 23\n")
    engine.chmod(0o755)
    env = {**os.environ, "BOOKS_APP_DIR": root}
    completed = subprocess.run(["/bin/sh", str(script)], env=env, timeout=10)
    assert completed.returncode == 23, completed.returncode
    log = (app / "startup.log").read_text()
    assert "Books launch" in log and "books-engine-running" in log, log

    engine.unlink()
    failed = subprocess.run(["/bin/sh", str(script)], env=env, timeout=10)
    assert failed.returncode != 0
    log = (app / "startup.log").read_text()
    assert "books-engine missing" in log, log
    assert "not found" in log or "No such file" in log, log

    engine.write_text("#!/bin/sh\nexit 0\n")
    engine.chmod(0o755)
    (app / "startup.log").write_text("x" * 65537)
    restarted = subprocess.run(["/bin/sh", str(script)], env=env, timeout=10)
    assert restarted.returncode == 0
    assert (app / "startup.previous.log").stat().st_size == 65537
print("Q2 shell launcher: executable, logging, exec failure, and rotation OK")
