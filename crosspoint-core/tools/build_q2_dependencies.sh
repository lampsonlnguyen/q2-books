#!/usr/bin/env bash
# Build static zlib and Expat for the exact Q2 target ABI without linking to
# Ubuntu's incompatible legacy-NaN MIPS runtime.
set -euo pipefail
sysroot="${Q2_SYSROOT:-$HOME/.cache/q2/sysroot}"
out="${1:-build/q2-reader-native/deps}"
cc="${Q2_CC:-clang}"
ar="${Q2_AR:-llvm-ar}"
flags="--target=mipsel-linux-gnu --sysroot=$sysroot -march=mips32r2 -mabi=32 -mhard-float -mfp64 -mnan=2008 -mabs=2008 -O2"
for tool in cmake git "$cc" "$ar"; do
  command -v "$tool" >/dev/null || { echo "Missing tool: $tool" >&2; exit 1; }
done
[[ -d "$sysroot/usr/include" ]] || { echo "Missing Q2 C headers: $sysroot/usr/include" >&2; exit 1; }
mkdir -p "$out"
out="$(cd "$out" && pwd)"
mkdir -p "$out/src"
if [[ ! -d "$out/src/zlib/.git" ]]; then
  git clone --depth 1 --branch v1.3.1 https://github.com/madler/zlib.git "$out/src/zlib"
fi
if [[ ! -d "$out/src/libexpat/.git" ]]; then
  git clone --depth 1 --branch R_2_6_4 https://github.com/libexpat/libexpat.git "$out/src/libexpat"
fi
common=(
  "-DCMAKE_SYSTEM_NAME=Linux"
  "-DCMAKE_SYSTEM_PROCESSOR=mipsel"
  "-DCMAKE_C_COMPILER=$cc"
  "-DCMAKE_C_FLAGS=$flags"
  "-DCMAKE_AR=$ar"
  "-DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY"
  "-DCMAKE_BUILD_TYPE=Release"
  "-DCMAKE_INSTALL_PREFIX=$out/install"
  "-DBUILD_SHARED_LIBS=OFF"
)
cmake -S "$out/src/zlib" -B "$out/zlib-build" "${common[@]}" \
  -DZLIB_BUILD_EXAMPLES=OFF
cmake --build "$out/zlib-build" --parallel 2
cmake --install "$out/zlib-build"

cmake -S "$out/src/libexpat/expat" -B "$out/expat-build" "${common[@]}" \
  -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_DOCS=OFF \
  -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_EXAMPLES=OFF \
  -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_PKGCONFIG=OFF \
  -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$out/expat-build" --parallel 2
cmake --install "$out/expat-build"

test -s "$out/install/lib/libz.a"
test -s "$out/install/lib/libexpat.a"
test -s "$out/install/include/zlib.h"
test -s "$out/install/include/expat.h"
echo "Static Q2-compatible dependencies built:"
echo "  Q2_ZLIB_A=$out/install/lib/libz.a"
echo "  Q2_EXPAT_A=$out/install/lib/libexpat.a"
echo "  Q2_DEPS_INCLUDE=$out/install/include"
