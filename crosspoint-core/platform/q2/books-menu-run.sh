#!/bin/sh
# SPDX-License-Identifier: MIT
# Q2 Pod waits for run to return, then connects to the browser socket.
ROOT=${BOOKS_APP_DIR:-/mnt/mmc/.books}
BOOKS_STATE_DIR="$ROOT" "$ROOT/books-engine" --native-menu </dev/null >/dev/null 2>&1 &
