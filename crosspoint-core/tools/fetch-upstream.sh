#!/usr/bin/env bash
set -euo pipefail
# Checkout only original CrossPoint reader source families. This is a source
# reference for the Linux port, NOT an ESP32 firmware build or runtime.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="$ROOT/upstream"
REVISION="9ac3475733e677b7f61ba3f6e8ce41ce1b9b2967"
if [[ ! -d "$UPSTREAM/.git" ]]; then
  git clone --filter=blob:none --sparse https://github.com/crosspoint-reader/crosspoint-reader.git "$UPSTREAM"
fi
git -C "$UPSTREAM" sparse-checkout init --cone
git -C "$UPSTREAM" sparse-checkout set \
  lib/Utf8 lib/Txt lib/Epub lib/EpdFont lib/GfxRenderer \
  src/activities/reader
git -C "$UPSTREAM" fetch --no-tags origin "$REVISION"
git -C "$UPSTREAM" checkout --detach "$REVISION"
printf 'CrossPoint reader-source reference: %s\n' "$(git -C "$UPSTREAM" rev-parse HEAD)"
echo "No ESP32 firmware, Arduino/FreeRTOS executable or e-ink driver is built or packaged."
