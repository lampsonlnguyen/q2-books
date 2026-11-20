#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
base="$PWD"
work="$base/build/q2-device"
mkdir -p "$work"
url=https://github.com/DiamondBond/q2-pod/releases/download/1.8.0/update.tar
hash=c4dcd6401d31da6da1fc74015e34f2d6b6a19e860bb6970262798aa23fb4c148
curl -fL --retry 3 "$url" -o "$work/update.tar"
echo "$hash  $work/update.tar" | sha256sum -c -
tar -xf "$work/update.tar" -C "$work" recovery-update/rootfs.squashfs
root="$work/rootfs"
unsquashfs -no-xattrs -f -d "$root" "$work/recovery-update/rootfs.squashfs" >/dev/null
test -e "$root/lib/libc.so.6"
test -e "$root/lib/ld-linux-mipsn8.so.1"
find "$root/lib" "$root/usr/lib" -name 'libstdc++.so.6*' -print -quit | grep -q . ||
  { echo "::error::Firmware has no Q2-compatible libstdc++ runtime"; exit 1; }
git clone --quiet --depth 1 --branch shanlingq2 https://github.com/DiamondBond/q2-librespot.git "$work/librespot"
test "$(git -C "$work/librespot" rev-parse HEAD)" = 4775f9c0b8f02cb76166482abb0d0dc5416f572f
mkdir -p "$work/headers/gnu"
cp /usr/mipsel-linux-gnu/include/gnu/stubs-o32_hard.h "$work/headers/gnu/stubs-o32_hard_2008.h"
flags="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mabicalls -O2 -fPIC -isystem $work/headers -isystem /usr/mipsel-linux-gnu/include"
clang $flags -c "$work/librespot/contrib/shanlingq2/start.c" -o "$work/start.o"
clang $flags -c "$work/librespot/contrib/shanlingq2/nonshared.c" -o "$work/nonshared.o"
llvm-ar rcs "$root/lib/libq2nonshared.a" "$work/nonshared.o"
for name in pthread.so.0 m.so.6 dl.so.2 rt.so.1 util.so.1 gcc_s.so.1; do
  if test -e "$root/lib/lib$name"; then
    short="$(echo "$name" | sed 's/\.[^.]*$//')"
    ln -sfn "lib$name" "$root/lib/lib$short"
  fi
done
echo 'GROUP ( libc.so.6 libq2nonshared.a AS_NEEDED ( ld-linux-mipsn8.so.1 ) )' > "$root/lib/libc.so"
link="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mabicalls -fuse-ld=lld -nostartfiles -nodefaultlibs -no-pie -Wl,--dynamic-linker=/lib/ld-linux-mipsn8.so.1 -L$root/lib -L$root/usr/lib $work/start.o -lstdc++ -lm -lpthread -ldl -lrt -lc -lgcc_s"
git clone --quiet --depth 1 --branch v1.3.1 https://github.com/madler/zlib.git "$work/zlib"
git clone --quiet --depth 1 --branch R_2_6_4 https://github.com/libexpat/libexpat.git "$work/expat"
prefix="$work/deps"
cmake -S "$work/zlib" -B "$work/zlib-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang "-DCMAKE_C_FLAGS=$flags" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_INSTALL_PREFIX=$prefix" -DZLIB_BUILD_EXAMPLES=OFF
cmake --build "$work/zlib-build" --parallel 2
cmake --install "$work/zlib-build"
cmake -S "$work/expat/expat" -B "$work/expat-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang "-DCMAKE_C_FLAGS=$flags" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_INSTALL_PREFIX=$prefix" -DEXPAT_SHARED_LIBS=OFF \
  -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF \
  -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_PKGCONFIG=OFF -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work/expat-build" --parallel 2
cmake --install "$work/expat-build"
printf '#include <vector>\nint main(){std::vector<int> x(2);return x[0];}\n' > "$work/probe.cpp"
clang++ $flags "$work/probe.cpp" $link -o "$work/probe"
python3 crosspoint-core/tools/check_q2_elf.py "$work/probe" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
cmake -S crosspoint-core -B "$work/reader" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$link" \
  "-DZLIB_LIBRARY=$prefix/lib/libz.a" "-DZLIB_INCLUDE_DIR=$prefix/include" \
  "-DEXPAT_LIBRARY=$prefix/lib/libexpat.a" "-DEXPAT_INCLUDE_DIR=$prefix/include"
cmake --build "$work/reader" --target crosspod-reader --parallel 2
elf="$work/reader/crosspod-reader"
python3 crosspoint-core/tools/check_q2_elf.py "$elf" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
timeout 60s qemu-mipsel -L "$root" "$elf" --validate "$work/reader/multilingual.epub"
timeout 60s qemu-mipsel -L "$root" "$elf" --walk "$work/reader/multilingual.epub"
timeout 60s qemu-mipsel -L "$root" "$elf" --render "$work/reader/vietnamese-prose.epub" "$work/vietnamese.pgm"
timeout 60s qemu-mipsel -L "$root" "$elf" --render-images "$work/reader/illustrated.epub" "$work/illustrated.pgm"
python3 - "$work/vietnamese.pgm" "$work/illustrated.pgm" <<'PY'
from pathlib import Path
import sys
for p in sys.argv[1:]:
    d=Path(p).read_bytes()
    assert d[:15]==b'P5\n375 320\n255\n'
    assert len(d)==120015 and d[15:].count(0)>30
PY
mkdir -p "$work/books"
cp "$work/reader/vietnamese-prose.epub" "$work/books/"
timeout 30s qemu-mipsel -L "$root" "$elf" --card-preview "$work/books" "$work/menu.pgm"
test -s "$work/menu.pgm"
card="$base/dist/card"
mkdir -p "$card/.crosspod" "$card/Books"
cp "$elf" "$card/.crosspod/crosspod-engine"
cp crosspoint-core/platform/q2/crosspod-launcher.sh "$card/.crosspod/q2-crosspod"
printf 'name=CrossPod\ngame=1\nrow=library\n' > "$card/.crosspod/q2app"
cp docs/q2-crosspod-card-app.md "$card/.crosspod/README.txt"
cp "$work/reader/vietnamese-prose.epub" "$card/Books/"
chmod +x "$card/.crosspod/q2-crosspod" "$card/.crosspod/crosspod-engine"
(cd "$card" && zip -q -r "$base/dist/crosspod-q2-reader-alpha.zip" .crosspod Books)
sha256sum "$base/dist/crosspod-q2-reader-alpha.zip" > "$base/dist/crosspod-q2-reader-alpha.zip.sha256"
unzip -t "$base/dist/crosspod-q2-reader-alpha.zip"