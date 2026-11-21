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
flags="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mabicalls -O2 -fPIC -D_FILE_OFFSET_BITS=64 -D_GLIBCXX_USE_CXX11_ABI=0 -isystem $work/headers -isystem /usr/mipsel-linux-gnu/include"
clang $flags -c "$work/librespot/contrib/shanlingq2/start.c" -o "$work/start.o"
clang $flags -c "$work/librespot/contrib/shanlingq2/nonshared.c" -o "$work/nonshared.o"
llvm-ar rcs "$root/lib/libq2nonshared.a" "$work/nonshared.o"
clang $flags -c crosspoint-core/platform/q2/q2_glibc_compat.c -o "$work/q2_glibc_compat.o"
clang++ $flags -c crosspoint-core/platform/q2/q2_from_chars_compat.cpp -o "$work/q2_from_chars_compat.o"
for name in pthread.so.0 m.so.6 dl.so.2 rt.so.1 util.so.1 gcc_s.so.1; do
  if test -e "$root/lib/lib$name"; then
    short="$(echo "$name" | sed 's/\.[^.]*$//')"
    ln -sfn "lib$name" "$root/lib/lib$short"
  fi
done
echo 'GROUP ( libc.so.6 libq2nonshared.a AS_NEEDED ( ld-linux-mipsn8.so.1 ) )' > "$root/lib/libc.so"
link="--target=mipsel-linux-gnu -march=mips32r2 -mabi=32 -mfp64 -mnan=2008 -mabs=2008 -mabicalls -fuse-ld=lld -nostartfiles -nodefaultlibs -no-pie -Wl,--dynamic-linker=/lib/ld-linux-mipsn8.so.1 -L$root/lib -L$root/usr/lib $work/start.o $work/q2_glibc_compat.o $work/q2_from_chars_compat.o -lstdc++ -lm -lpthread -ldl -lrt -lc -lgcc_s"
git clone --quiet --depth 1 --branch v1.3.1 https://github.com/madler/zlib.git "$work/zlib"
git clone --quiet --depth 1 --branch R_2_6_4 https://github.com/libexpat/libexpat.git "$work/expat"
prefix="$work/deps"
cmake -S "$work/zlib" -B "$work/zlib-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang "-DCMAKE_C_FLAGS=$flags" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_INSTALL_PREFIX=$prefix" -DZLIB_BUILD_EXAMPLES=OFF
# zlib 1.3.1 also defines a shared target even when the consumer needs
# only the static archive. Building 'all' links that unused shared target
# against Ubuntu's legacy-NaN CRT and fails. Never build that target.
cmake --build "$work/zlib-build" --target zlibstatic --parallel 2
mkdir -p "$prefix/lib" "$prefix/include"
cp "$work/zlib-build/libz.a" "$prefix/lib/libz.a"
cp "$work/zlib/zlib.h" "$work/zlib-build/zconf.h" "$prefix/include/"
cmake -S "$work/expat/expat" -B "$work/expat-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang "-DCMAKE_C_FLAGS=$flags" \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_INSTALL_PREFIX=$prefix" -DEXPAT_SHARED_LIBS=OFF \
  -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_TESTS=OFF -DEXPAT_BUILD_DOCS=OFF \
  -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_PKGCONFIG=OFF -DCMAKE_INSTALL_LIBDIR=lib
cmake --build "$work/expat-build" --parallel 2
cmake --install "$work/expat-build"
# Pinned libjpeg-turbo source. Disable SIMD for MIPS32r2; the hardened
# grayscale decoder enforces a 1 megapixel / 4 MiB compressed-image limit.
git clone --quiet --depth 1 --branch 3.0.4 https://github.com/libjpeg-turbo/libjpeg-turbo.git "$work/libjpeg"
cmake -S "$work/libjpeg" -B "$work/jpeg-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=mipsel -DCMAKE_C_COMPILER=clang \
  "-DCMAKE_C_FLAGS=$flags" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib -DENABLE_SHARED=OFF -DENABLE_STATIC=ON \
  -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF -DWITH_JPEG8=ON
cmake --build "$work/jpeg-build" --target jpeg-static --parallel 2
# Only jpeg-static was built. Installing the entire CMake project expects
# unbuilt CLI tools such as cjpeg-static; copy precisely the verified library
# and its API headers, as with our zlibstatic build.
mkdir -p "$prefix/lib" "$prefix/include"
cp "$work/jpeg-build/libjpeg.a" "$prefix/lib/libjpeg.a"
cp "$work/libjpeg/jpeglib.h" "$work/libjpeg/jmorecfg.h" "$work/libjpeg/jerror.h" "$prefix/include/"
cp "$work/jpeg-build/jconfig.h" "$prefix/include/jconfig.h"
test -s "$prefix/lib/libjpeg.a"
printf '#include <vector>\nint main(){std::vector<int> x(2);return x[0];}\n' > "$work/probe.cpp"
clang++ $flags "$work/probe.cpp" $link -o "$work/probe"
python3 crosspoint-core/tools/check_q2_elf.py "$work/probe" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
cmake -S crosspoint-core -B "$work/reader" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$link" \
  "-DZLIB_LIBRARY=$prefix/lib/libz.a" "-DZLIB_INCLUDE_DIR=$prefix/include" \
  "-DEXPAT_LIBRARY=$prefix/lib/libexpat.a" "-DEXPAT_INCLUDE_DIR=$prefix/include" \
  "-DJPEG_LIBRARY=$prefix/lib/libjpeg.a" "-DJPEG_INCLUDE_DIR=$prefix/include"
cmake --build "$work/reader" --target crosspod-reader --parallel 2
cmake --build "$work/reader" --target crosspod_png_formats_tests --parallel 2
timeout 30s qemu-mipsel -L "$root" "$work/reader/crosspod_png_formats_tests"
elf="$work/reader/crosspod-reader"
python3 crosspoint-core/tools/check_q2_elf.py "$elf" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
# A never-opened EPUB must acquire its OPF title, author and cover without
# constructing a CrossPoint Section, and a second pass must reuse the cache.
python3 crosspoint-core/tests/make_shelf_index_epub.py "$work/reader/shelf-cover.epub"
mkdir -p "$work/idle-index-state"
for pass in fresh cached; do
  CROSSPOD_STATE_DIR="$work/idle-index-state" timeout 60s \
    qemu-mipsel -L "$root" "$elf" --index-book "$work/reader/shelf-cover.epub" \
    | tee "$work/shelf-index-$pass.log"
  grep -q 'metadata=1 cover=1' "$work/shelf-index-$pass.log"
done
grep -q 'updated=1' "$work/shelf-index-fresh.log"
grep -q 'updated=0' "$work/shelf-index-cached.log"
grep -q 'Nguyễn Thị Mai' "$work/shelf-index-fresh.log"
echo "Q2 unattended OPF/cover index: PASS"
echo "C++ ABI requirements:"
mipsel-linux-gnu-readelf -Ws "$elf" | grep 'GLIBCXX_' | sort -u | tail -25 || true
timeout 60s qemu-mipsel -L "$root" "$elf" --validate "$work/reader/multilingual.epub" | tee "$work/upstream-validation.log"
grep -q "CrossPod original Section:" "$work/upstream-validation.log"
# The prior collector stays as a rescue mode for incompatible EPUBs.
CROSSPOD_ENGINE=legacy timeout 60s qemu-mipsel -L "$root" "$elf" \
  --validate "$work/reader/multilingual.epub" | tee "$work/legacy-validation.log"
grep -q "CrossPod legacy fallback:" "$work/legacy-validation.log"
timeout 60s qemu-mipsel -L "$root" "$elf" --walk "$work/reader/multilingual.epub"

# Real MIPS regression: show the first page before finishing a long chapter;
# then traverse every page forward and backward without skipping an unfinished
# Section watermark. No framebuffer simulation threads or busy waiting.
timeout 120s qemu-mipsel -L "$root" "$elf" \
  --validate "$work/reader/long-chapter.epub" | tee "$work/incremental-first-page.log"
grep -q '(incremental)' "$work/incremental-first-page.log"
timeout 120s qemu-mipsel -L "$root" "$elf" \
  --walk "$work/reader/long-chapter.epub" | tee "$work/incremental-walk.log"
# Wheel-turn stability gate: render *every* page, not only update the
# page index. Repeated fast turns previously left the display blank.
timeout 180s qemu-mipsel -L "$root" "$elf" \
  --render-walk "$work/reader/long-chapter.epub" \
  | tee "$work/long-scroll-render.log"
grep -q 'without blank text' "$work/long-scroll-render.log"
python3 - "$work/incremental-walk.log" <<'PY'
from pathlib import Path
import re, sys
match = re.search(r"CrossPod walked (\d+) pages forward and (\d+) backward",
                  Path(sys.argv[1]).read_text())
assert match, "No complete navigation trace"
forward, backward = map(int, match.groups())
assert forward >= 10 and forward == backward, (forward, backward)
print(f"MIPS Q2 incremental long EPUB: PASS ({forward} page turns)")
PY


# Exercise the same bounded streaming path on actual Q2 MIPS ABI, including
# multi-megabyte comments, invalid XML and oversized chapter rejection.
cat > "$work/reader-qemu" <<EOF
#!/bin/sh
exec qemu-mipsel -L "$root" "$elf" "\$@"
EOF
chmod +x "$work/reader-qemu"
python3 crosspoint-core/tests/xhtml_streaming_test.py \
  "$work/reader-qemu" "$work/reader/illustrated.epub"
timeout 60s qemu-mipsel -L "$root" "$elf" --render "$work/reader/vietnamese-prose.epub" "$work/vietnamese.pgm"
# v0.4.0 performance instrumentation: run twice using the same per-book
# cache and retain both measurements (cold versus warmed SD layout).
mkdir -p "$work/benchmark-state"
for phase in cold warm; do
  CROSSPOD_STATE_DIR="$work/benchmark-state" timeout 90s \
    qemu-mipsel -L "$root" "$elf" --bench "$work/reader/vietnamese-prose.epub" \
    | tee "$work/bench-$phase.log"
  grep -q 'first_render_us=' "$work/bench-$phase.log"
  grep -q 'cached_render_us=' "$work/bench-$phase.log"
  grep -q 'read_blocks=' "$work/bench-$phase.log"
done
# Check that EPUB layout settings really affect the *upstream* page raster.
# Older CrossPod silently overwrote the saved CSS/hyphenation/spacing settings.
mkdir -p "$work/layout-settings"
printf 'CROSSPOD-SETTINGS2 14 100 0 0 1 0\\n' > "$work/layout-settings/reading-settings.conf"
CROSSPOD_STATE_DIR="$work/layout-settings" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --render "$work/reader/vietnamese-prose.epub" \
    "$work/default-settings.pgm"
printf 'CROSSPOD-SETTINGS2 14 140 1 1 0 0\\n' > "$work/layout-settings/reading-settings.conf"
CROSSPOD_STATE_DIR="$work/layout-settings" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --render "$work/reader/vietnamese-prose.epub" \
    "$work/changed-settings.pgm"
python3 - "$work/default-settings.pgm" "$work/changed-settings.pgm" <<'PY'
from pathlib import Path
import sys
a,b=(Path(p).read_bytes() for p in sys.argv[1:])
assert len(a)==len(b)==120015
changed=sum(x!=y for x,y in zip(a[15:],b[15:]))
assert changed>50, f"Layout settings ignored: only {changed} changed pixels"
print(f"MIPS EPUB CSS/layout preferences: PASS ({changed} pixel changes)")
PY
timeout 60s qemu-mipsel -L "$root" "$elf" --render-images "$work/reader/illustrated.epub" "$work/illustrated.pgm" | tee "$work/images.log"
grep -q "CrossPod original Section:" "$work/images.log"
grep -q "CrossPod rendered images: 2" "$work/images.log"
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
# The Q2 search screen must render under the real MIPS ABI, and the
# recursive scanner must find books below the old one-folder limit.
mkdir -p "$work/books/Deep/Level/Three"
cp "$work/reader/vietnamese-prose.epub" \
  "$work/books/Deep/Level/Three/Nested.EPUB"
timeout 30s qemu-mipsel -L "$root" "$elf" --list-books "$work/books" \
  | tee "$work/library-discovery.log"
grep -q '/Deep/Level/Three/Nested.EPUB' "$work/library-discovery.log"
timeout 30s qemu-mipsel -L "$root" "$elf" \
  --card-search-preview "$work/books" "$work/search.pgm"
python3 - "$work/menu.pgm" "$work/search.pgm" <<'PY'
from pathlib import Path
import sys
home,search=(Path(p).read_bytes() for p in sys.argv[1:])
assert len(home)==len(search)==120015
assert home[:15]==search[:15]==b"P5\n375 320\n255\n"
assert sum(a!=b for a,b in zip(home[15:],search[15:]))>500
print("Q2 searchable library screen: PASS")
PY
card="$base/dist/card"
mkdir -p "$card/.crosspod" "$card/Books"
cp "$elf" "$card/.crosspod/crosspod-engine"
cp crosspoint-core/platform/q2/crosspod-launcher.sh "$card/.crosspod/q2-crosspod"
printf 'name=CrossPod\ngame=1\nrow=library\n' > "$card/.crosspod/q2app"
cp docs/q2-crosspod-card-app.md "$card/.crosspod/README.txt"
cp "$work/reader/vietnamese-prose.epub" "$card/Books/"
# Mandatory font-visibility book in EVERY Q2 test-build ZIP. Do not put it
# inside .crosspod, which the library scanner deliberately treats as hidden.
# One compact XHTML paragraph per language; no external web fonts required.
python3 crosspoint-core/tests/make_language_visibility_epub.py \
  "$card/Books/CrossPod-Multilingual-Visibility-Test.epub"
python3 crosspoint-core/tests/make_language_visibility_epub.py \
  "$card/Books/CrossPod-Multilingual-Visibility-Test.epub" --check
# Original CrossPoint font files, pinned to verified official SHA-256 digests.
python3 crosspoint-core/tools/fetch_q2_fonts.py "$card/.crosspod/fonts"
cp crosspoint-core/lib/EpdFont/builtinFonts/NotoSans-OFL.txt "$card/.crosspod/NotoSans-OFL.txt"
# Original CrossPoint Page links, internal anchors, EPUB footnotes and
# one-entry return history must work on the actual Q2 MIPS executable.
python3 crosspoint-core/tests/make_reader_links_epub.py "$work/reader/page-links.epub"
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --test-page-links "$work/reader/page-links.epub" \
  | tee "$work/page-links.log"
grep -q 'page link jump and return: PASS' "$work/page-links.log"
# Selection must use the original rendered TextBlock source ranges, write
# and re-read highlight records, then remove them without changing the EPUB.
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --test-selection "$work/reader/page-links.epub" \
  | tee "$work/selection.log"
grep -q 'unicode selection and persistent highlight: PASS' "$work/selection.log"


# Validate the generated EPUB on the actual Q2 MIPS application, with the
# full SD cpfont bundle present. This checks structure/openability; visual
# correctness is still assessed by eye on the real Shanling LCD.
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --validate "$card/Books/CrossPod-Multilingual-Visibility-Test.epub" \
  | tee "$work/language-visibility.log"
grep -q 'CrossPod original Section:' "$work/language-visibility.log"



# Full script pack must load on MIPS, not just be copied into the ZIP.
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --validate "$work/reader/script-korean.epub" | tee "$work/korean-validate.log"
grep -q 'CrossPod multilingual fonts: 14/14 scripts loaded (12pt)' "$work/korean-validate.log"
grep -q 'CrossPod original Section:' "$work/korean-validate.log"

for script in korean devanagari; do
  file="$work/reader/script-$script.epub"
  CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
    --render "$file" "$work/$script-enabled.pgm" | tee "$work/$script-enabled.log"
  CROSSPOD_DISABLE_SCRIPTS="$script" CROSSPOD_STATE_DIR="$card/.crosspod" \
    timeout 90s qemu-mipsel -L "$root" "$elf" \
    --render "$file" "$work/$script-disabled.pgm" | tee "$work/$script-disabled.log"
  grep -q 'CrossPod original Section:' "$work/$script-enabled.log"
  python3 - "$work/$script-enabled.pgm" "$work/$script-disabled.pgm" "$script" <<'PY'
from pathlib import Path
import sys
active, disabled = (Path(p).read_bytes() for p in sys.argv[1:3])
assert active.startswith(b"P5\n375 320\n255\n")
assert len(active)==len(disabled)==120015
delta=sum(a!=b for a,b in zip(active[15:],disabled[15:]))
assert delta>100, f"{sys.argv[3]} glyph fallback ineffective: {delta}"
print(f"Q2 script raster {sys.argv[3]}: PASS ({delta} changed pixels)")
PY
done


# Genuine Chinese/Japanese regression: the pinned CrossPoint CJK font must be
# loaded under the real Q2 MIPS ABI. Compare exact rasters against the
# explicitly disabled-fallback control, not just the process exit code.
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/cjk-script.epub" "$work/cjk-active.pgm" \
  | tee "$work/cjk-active.log"
grep -q 'CrossPod original Section:' "$work/cjk-active.log"
grep -q 'CrossPod CJK fallback: original CrossPoint NotoSansCJKJP cpfont 12pt' "$work/cjk-active.log"
CROSSPOD_DISABLE_CJK=1 CROSSPOD_STATE_DIR="$card/.crosspod" \
  timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/cjk-script.epub" "$work/cjk-disabled.pgm" \
  | tee "$work/cjk-disabled.log"
grep -q 'CrossPod CJK fallback: unavailable 12pt' "$work/cjk-disabled.log"
python3 - "$work/cjk-active.pgm" "$work/cjk-disabled.pgm" <<'PY'
from pathlib import Path
import sys
active, disabled = (Path(p).read_bytes() for p in sys.argv[1:])
for page in (active, disabled):
    assert page.startswith(b"P5\n375 320\n255\n")
    assert len(page) == 120015
    assert page[15:].count(0) > 100
changed = sum(a != b for a, b in zip(active[15:], disabled[15:]))
assert changed > 200, f"CJK fallback ineffective: only {changed} changed pixels"
print(f"MIPS Q2 Chinese/Japanese glyph fallback: PASS ({changed} pixel differences)")
PY

# The default must now use the extended multilingual Sans 12 pack, not just
# the much smaller built-in UI glyph set.
CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/vietnamese-prose.epub" "$work/default-extended.pgm" \
  | tee "$work/default-extended.log"
grep -q 'CrossPod font source: original CrossPoint extended cpfont' "$work/default-extended.log"
# Run the compiled Q2 reader twice to verify typography selection and raster.
for setting in "sans 12" "serif 12"; do
  echo "CROSSPOD-TYPE1 $setting" > "$card/.crosspod/typography.conf"
  slug="$(echo "$setting" | tr ' ' '-')"
  CROSSPOD_STATE_DIR="$card/.crosspod" timeout 90s qemu-mipsel -L "$root" "$elf" \
    --render "$work/reader/vietnamese-prose.epub" "$work/$slug.pgm" \
    | tee "$work/$slug.log"
done
python3 - "$work/sans-12.pgm" "$work/serif-12.pgm" "$work/sans-12.log" "$work/serif-12.log" <<'PY'
from pathlib import Path
import sys
sans, serif = (Path(p).read_bytes() for p in sys.argv[1:3])
for data in (sans, serif):
    assert data.startswith(b"P5\n375 320\n255\n")
    assert len(data) == 120015
    assert data[15:].count(0) > 100
assert sans != serif, "Font choices must produce distinct glyph rasters"
logs = [Path(p).read_text() for p in sys.argv[3:]]
assert "CrossPod typeface: Sans 12pt" in logs[0], logs[0]
assert "CrossPod typeface: Serif 12pt" in logs[1], logs[1]
assert all("Selected font file missing" not in log and "Font file not installed" not in log for log in logs), logs
print("MIPS Q2 font switching and rendering: PASS")
PY
rm -f "$card/.crosspod/typography.conf"
# Never ship QEMU-generated Section/page caches from the CI sample book.
rm -rf -- "$card/.crosspod/cache"
chmod +x "$card/.crosspod/q2-crosspod" "$card/.crosspod/crosspod-engine"
(cd "$card" && zip -q -r "$base/dist/crosspod-q2-reader-alpha.zip" .crosspod Books)
sha256sum "$base/dist/crosspod-q2-reader-alpha.zip" > "$base/dist/crosspod-q2-reader-alpha.zip.sha256"
unzip -t "$base/dist/crosspod-q2-reader-alpha.zip"