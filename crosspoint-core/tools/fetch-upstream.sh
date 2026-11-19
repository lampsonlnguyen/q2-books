#!/usr/bin/env bash
set -euo pipefail

# Fetch the *actual* CrossPoint tree at the reviewed revision.
# Does not fetch the old CrossPod prototype or Q2 Pod firmware.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="$ROOT/upstream"
REVISION="9ac3475733e677b7f61ba3f6e8ce41ce1b9b2967"
if [[ -e "$UPSTREAM/.git" ]]; then
  git -C "$UPSTREAM" fetch --no-tags origin "$REVISION"
else
  git clone --filter=blob:none https://github.com/crosspoint-reader/crosspoint-reader.git "$UPSTREAM"
fi
git -C "$UPSTREAM" checkout --detach "$REVISION"
printf 'CrossPoint original source: %s\n' "$(git -C "$UPSTREAM" rev-parse HEAD)"
echo 'Next: build CrossPoint EPUB/activities against the POSIX/Q2 HAL (not yet implemented).'
