#!/usr/bin/env bash
# CrossPod hardware diagnostic only. Never builds/flashes firmware or a reader.
set -euo pipefail

: "${Q2_SYSROOT:=$HOME/.cache/q2/sysroot}"
: "${Q2_CXX:=mipsel-linux-gnu-g++}"
: "${Q2_CC:=mipsel-linux-gnu-gcc}"

if [[ ! -d "$Q2_SYSROOT" ]]; then
  printf 'Q2 sysroot missing: %s\nSet Q2_SYSROOT to your verified Q2 Linux sysroot.\n' "$Q2_SYSROOT" >&2
  exit 2
fi
if ! command -v "$Q2_CC" >/dev/null 2>&1; then
  printf 'MIPS C compiler missing: %s\\nSet Q2_CC to your MIPS C compiler.\\n' "$Q2_CC" >&2
  exit 2
fi
if ! command -v "$Q2_CXX" >/dev/null 2>&1; then
  printf 'MIPS C++ compiler missing: %s\nSet Q2_CXX to your MIPS C++ compiler.\n' "$Q2_CXX" >&2
  exit 2
fi

root="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
out="${1:-$root/../../../build/q2-device-check}"
mkdir -p "$out"
cmake -S "$root" -B "$out" \
  -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=mipsel \
  -DCMAKE_SYSROOT="$Q2_SYSROOT" \
  -DCMAKE_C_COMPILER="$Q2_CC" \
  -DCMAKE_CXX_COMPILER="$Q2_CXX" \
  -DCMAKE_C_FLAGS="-march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008" \
  -DCMAKE_CXX_FLAGS="-march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build "$out" --parallel 2
file "$out/crosspod-hwcheck"
printf '\nDiagnostic only — verify device GLIBC/GLIBCXX versions using readelf -V before running.\n'
