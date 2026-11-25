#!/usr/bin/env bash
# Build the real Books reader using a Shanling-Q2-compatible MIPS32r2 sysroot.
# This script deliberately refuses to use Ubuntu's legacy-NaN MIPS runtime.
set -euo pipefail

if [[ ${1:-} == "--help" || $# -gt 1 ]]; then
  echo "usage: Q2_SYSROOT=/path/to/q2/sysroot [Q2_CC=clang Q2_CXX=clang++] $0 [output-dir]"
  exit 0
fi

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
sysroot="${Q2_SYSROOT:-$HOME/.cache/q2/sysroot}"
out="${1:-$repo_root/build/q2-reader-native}"
cc="${Q2_CC:-clang}"
cxx="${Q2_CXX:-clang++}"
readelf="${Q2_READELF:-llvm-readelf}"
objdump="${Q2_OBJDUMP:-llvm-objdump}"
python="${PYTHON:-python3}"
flags="--target=mipsel-linux-gnu --sysroot=$sysroot -march=mips32r2 -mabi=32 -mhard-float -mfp64 -mnan=2008 -mabs=2008"
linkflags="--target=mipsel-linux-gnu --sysroot=$sysroot -march=mips32r2 -mabi=32 -mhard-float -mfp64 -mnan=2008 -mabs=2008 -fuse-ld=lld -static"

for command in cmake "$cc" "$cxx" "$readelf" "$objdump" "$python" zip; do
  command -v "$command" >/dev/null || { echo "Missing tool: $command" >&2; exit 1; }
done
[[ -d "$sysroot/usr/include" ]] || { echo "Q2 sysroot missing: $sysroot" >&2; exit 1; }

# Build missing zlib/Expat archives with Q2's NaN-2008 ABI, never the
# Ubuntu mipsel default legacy-NaN libraries.
zlib="${Q2_ZLIB_A:-$sysroot/usr/lib/libz.a}"
expat="${Q2_EXPAT_A:-$sysroot/usr/lib/libexpat.a}"
includes="${Q2_DEPS_INCLUDE:-$sysroot/usr/include}"
if [[ ! -f "$zlib" || ! -f "$expat" ]]; then
  echo "Q2 zlib/Expat archive missing; compiling from pinned sources..."
  bash "$repo_root/crosspoint-core/tools/build_q2_dependencies.sh" "$out/deps"
  deps="$(cd "$out/deps/install" && pwd)"
  if [[ ! -f "$zlib" ]]; then zlib="$deps/lib/libz.a"; fi
  if [[ ! -f "$expat" ]]; then expat="$deps/lib/libexpat.a"; fi
  includes="$deps/include"
fi
for file in "$zlib" "$expat" "$includes/zlib.h" "$includes/expat.h"; do
  [[ -f "$file" ]] || { echo "Q2 dependency missing: $file" >&2; exit 1; }
done
mkdir -p "$out"
printf '#include <vector>\nint main() { std::vector<int> x(2); return x[0]; }\n' > "$out/abi_probe.cpp"
# Verify this sysroot can really link a C++ binary. An object-only test is
# insufficient to catch legacy-NaN crt/libstdc++ ABI conflicts.
"$cxx" $linkflags "$out/abi_probe.cpp" -o "$out/abi_probe"
"$python" "$repo_root/crosspoint-core/tools/check_q2_elf.py" \
  "$out/abi_probe" "$readelf" "$objdump"

cmake -S "$repo_root/crosspoint-core" -B "$out/build" \
  -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=mipsel \
  -DCMAKE_C_COMPILER="$cc" -DCMAKE_CXX_COMPILER="$cxx" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_FLAGS="$flags" -DCMAKE_CXX_FLAGS="$flags" \
  -DCMAKE_EXE_LINKER_FLAGS="$linkflags" \
  -DZLIB_LIBRARY="$zlib" -DZLIB_INCLUDE_DIR="$includes" \
  -DEXPAT_LIBRARY="$expat" -DEXPAT_INCLUDE_DIR="$includes"
cmake --build "$out/build" --target books-reader --parallel 2

reader="$out/build/books-reader"
"$python" "$repo_root/crosspoint-core/tools/check_q2_elf.py" \
  "$reader" "$readelf" "$objdump"
mkdir -p "$out/card/.books"
cp "$reader" "$out/card/.books/books-engine"
cp "$repo_root/crosspoint-core/platform/q2/books-launcher.sh" "$out/card/.books/q2-books"
cp "$repo_root/crosspoint-core/platform/q2/books-menu-run.sh" "$out/card/.books/run"
cp "$repo_root/crosspoint-core/platform/q2/books-native.q2app" "$out/card/.books/q2app"
cp "$repo_root/crosspoint-core/platform/q2/books-fullscreen.q2app" "$out/card/.books/q2app.fullscreen"
chmod +x "$out/card/.books/q2-books" "$out/card/.books/run"
cat > "$out/card/.books/README.txt" <<'NOTICE'
Books early hardware alpha — NOT a firmware update.
Requires Q2 Pod card apps with row=library support.
Extract the hidden .books folder to your microSD root.
Launch Library > Books; place EPUB files in microSD/Books/.
Older firmware without full-screen menu rows: copy .books/q2app.fullscreen over .books/q2app.
Some XHTML/CSS/image features are still incomplete.
Hold Return to escape if the card app becomes unresponsive.
Hardware operation is unverified until tested on a real Q2.
NOTICE
(
  cd "$out/card"
  zip -q -r "$out/books-q2-reader-alpha.zip" .books
)
sha256sum "$out/books-q2-reader-alpha.zip" > "$out/books-q2-reader-alpha.zip.sha256"
echo "Q2 reader alpha package: $out/books-q2-reader-alpha.zip"
