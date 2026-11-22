#!/bin/sh
# SPDX-License-Identifier: MIT
# Q2 Pod execl() launches this directly as .books/q2-books.
# /bin/sh exists on stock Q2; a script lets us record exec failures even
# when the MIPS reader ELF cannot start (ABI, EACCES, ENOEXEC, missing loader).
ROOT=${BOOKS_APP_DIR:-/mnt/mmc/.books}
LOG="$ROOT/startup.log"
BIN="$ROOT/books-engine"
# Keep Section HTML/page caches and font settings on SD, not the 128MB tmpfs.
export BOOKS_STATE_DIR="$ROOT"
if [ -f "$ROOT/profile-session" ]; then
  export BOOKS_PROFILE_SESSION=1
fi
# Do not block launch if card is read-only. Keep log size bounded across boots.
if ( : >> "$LOG" ) 2>/dev/null; then
  if [ "$(wc -c < "$LOG" 2>/dev/null)" -gt 65536 ] 2>/dev/null; then
    mv -f "$LOG" "$ROOT/startup.previous.log" 2>/dev/null || :
    : > "$LOG" 2>/dev/null || :
  fi
  {
    echo "===== Books launch ====="
    date -u '+UTC %Y-%m-%dT%H:%M:%SZ' 2>/dev/null || :
    uname -a 2>/dev/null || :
    echo "runner=/bin/sh"
    ls -l "$BIN" 2>/dev/null || echo "books-engine missing"
    grep -E '^(MemAvailable|MemFree|Shmem):' /proc/meminfo 2>/dev/null || :
    echo "starting books-engine"
  } >> "$LOG" 2>&1
  exec "$BIN" >> "$LOG" 2>&1
else
  exec "$BIN"
fi
