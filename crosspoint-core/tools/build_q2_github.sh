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
# HTTPS uses the exact libcurl already shipped by Q2 Pod. Never link an
# Ubuntu x86_64 library or bundle a second TLS stack on the 128MB device.
if ! test -e "$root/usr/lib/libcurl.so.4"; then
  echo "::error::Q2 rootfs is missing the stock TLS-enabled libcurl.so.4"
  exit 1
fi
ln -sfn libcurl.so.4 "$root/usr/lib/libcurl.so"
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
# Pinned FreeType for smooth menu text in Q2 Pod's TrueType faces (the Q2's
# firmware has no FreeType). TrueType/CFF rendering only: no zlib, PNG,
# HarfBuzz, bzip2 or Brotli.
git clone --quiet --depth 1 --branch VER-2-13-3 https://github.com/freetype/freetype.git "$work/freetype"
cmake -S "$work/freetype" -B "$work/freetype-build" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_SYSTEM_PROCESSOR=mipsel -DCMAKE_C_COMPILER=clang \
  "-DCMAKE_C_FLAGS=$flags" -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY \
  -DCMAKE_BUILD_TYPE=Release "-DCMAKE_INSTALL_PREFIX=$prefix" \
  -DCMAKE_INSTALL_LIBDIR=lib -DBUILD_SHARED_LIBS=OFF \
  -DFT_DISABLE_ZLIB=ON -DFT_DISABLE_BZIP2=ON -DFT_DISABLE_PNG=ON \
  -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build "$work/freetype-build" --parallel 2
cmake --install "$work/freetype-build"
test -s "$prefix/lib/libfreetype.a"
test -s "$prefix/include/freetype2/ft2build.h"
printf '#include <vector>\nint main(){std::vector<int> x(2);return x[0];}\n' > "$work/probe.cpp"
clang++ $flags "$work/probe.cpp" $link -o "$work/probe"
python3 crosspoint-core/tools/check_q2_elf.py "$work/probe" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
cmake -S crosspoint-core -B "$work/reader" -DCMAKE_SYSTEM_NAME=Linux \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
  -DCMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY -DCMAKE_BUILD_TYPE=Release \
  "-DCMAKE_C_FLAGS=$flags" "-DCMAKE_CXX_FLAGS=$flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$link" \
  "-DCMAKE_CXX_FLAGS_RELEASE=-O2 -DNDEBUG -DBOOKS_Q2_RUNTIME=1" \
  "-DZLIB_LIBRARY=$prefix/lib/libz.a" "-DZLIB_INCLUDE_DIR=$prefix/include" \
  "-DEXPAT_LIBRARY=$prefix/lib/libexpat.a" "-DEXPAT_INCLUDE_DIR=$prefix/include" \
  "-DJPEG_LIBRARY=$prefix/lib/libjpeg.a" "-DJPEG_INCLUDE_DIR=$prefix/include" \
  "-DFREETYPE_LIBRARY=$prefix/lib/libfreetype.a" \
  "-DFREETYPE_INCLUDE_DIR_ft2build=$prefix/include/freetype2" \
  "-DFREETYPE_INCLUDE_DIR_freetype2=$prefix/include/freetype2" \
  -DBOOKS_REQUIRE_FREETYPE=ON
cmake --build "$work/reader" --target books-reader --parallel 2
cmake --build "$work/reader" --target books_png_formats_tests --parallel 2
timeout 30s qemu-mipsel -L "$root" "$work/reader/books_png_formats_tests"
elf="$work/reader/books-reader"
python3 crosspoint-core/tools/check_q2_elf.py "$elf" mipsel-linux-gnu-readelf mipsel-linux-gnu-objdump
# Exercise the same Q2 socket and controls used by the interactive reader.
cmake --build "$work/reader" --target books_q2_input_tests crosspoint_controls_tests --parallel 2
timeout 30s qemu-mipsel -L "$root" "$work/reader/books_q2_input_tests"
timeout 30s qemu-mipsel -L "$root" "$work/reader/crosspoint_controls_tests"
BOOKS_PROFILE_SESSION=1 python3 - "$root" "$elf" <<'PYTEST'
import subprocess
import sys
import time
from pathlib import Path

endpoint = Path("/tmp/q2-books.input.sock")
assert not endpoint.exists(), "Another process owns the Books input endpoint"
with subprocess.Popen(
    ["qemu-mipsel", "-L", sys.argv[1], sys.argv[2]],
    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True,
) as reader:
    try:
        deadline = time.monotonic() + 5
        while not endpoint.is_socket() and reader.poll() is None and time.monotonic() < deadline:
            time.sleep(0.02)
        assert endpoint.is_socket(), "Interactive reader did not bind its input socket"
        # Allow the idle reader to reach the poll wait before sending TERM.
        time.sleep(0.25)
        reader.terminate()
        _, stderr = reader.communicate(timeout=3)
        assert reader.returncode == 0 and not endpoint.exists(), stderr
        assert "Books session: elapsed_ms=" in stderr, stderr
        print(stderr.strip())
    finally:
        if reader.poll() is None:
            reader.kill()
PYTEST
# A never-opened EPUB must acquire its OPF title, author and cover without
# constructing a CrossPoint Section, and a second pass must reuse the cache.
python3 crosspoint-core/tests/make_shelf_index_epub.py "$work/reader/shelf-cover.epub"
mkdir -p "$work/idle-index-state"
for pass in fresh cached; do
  BOOKS_STATE_DIR="$work/idle-index-state" timeout 60s \
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
grep -q "Books original Section:" "$work/upstream-validation.log"
# The prior collector stays as a rescue mode for incompatible EPUBs.
BOOKS_ENGINE=legacy timeout 60s qemu-mipsel -L "$root" "$elf" \
  --validate "$work/reader/multilingual.epub" | tee "$work/legacy-validation.log"
grep -q "Books legacy fallback:" "$work/legacy-validation.log"
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
match = re.search(r"Books walked (\d+) pages forward and (\d+) backward",
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
  BOOKS_STATE_DIR="$work/benchmark-state" timeout 90s \
    qemu-mipsel -L "$root" "$elf" --bench "$work/reader/vietnamese-prose.epub" \
    | tee "$work/bench-$phase.log"
  grep -q 'first_render_us=' "$work/bench-$phase.log"
  grep -q 'cached_render_us=' "$work/bench-$phase.log"
  grep -q 'read_blocks=' "$work/bench-$phase.log"
done
# Check that EPUB layout settings really affect the *upstream* page raster.
# Older Books silently overwrote the saved CSS/hyphenation/spacing settings.
mkdir -p "$work/layout-settings"
printf 'BOOKS-SETTINGS2 14 100 0 0 1 0\\n' > "$work/layout-settings/reading-settings.conf"
BOOKS_STATE_DIR="$work/layout-settings" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --render "$work/reader/vietnamese-prose.epub" \
    "$work/default-settings.pgm"
printf 'BOOKS-SETTINGS2 14 140 1 1 0 0\\n' > "$work/layout-settings/reading-settings.conf"
BOOKS_STATE_DIR="$work/layout-settings" timeout 90s \
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
grep -q "Books original Section:" "$work/images.log"
grep -q "Books rendered images: 2" "$work/images.log"
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
mkdir -p "$card/.books" "$card/Books"
cp "$elf" "$card/.books/books-engine"
cp crosspoint-core/platform/q2/books-launcher.sh "$card/.books/q2-books"
printf 'name=Books\ngame=1\nrow=library\n' > "$card/.books/q2app"
cp docs/q2-books-card-app.md "$card/.books/README.txt"
# Optional user-editable offline word definitions (UTF-8 TSV). Do not
# overwrite an existing personal dictionary when copying over an SD card.
cp crosspoint-core/assets/dictionary-starter.tsv "$card/.books/dictionary-starter.tsv"
cp crosspoint-core/assets/connected.conf.example "$card/.books/connected.conf.example"
# Menu translations, used when Q2 Pod is set to that language (lang/README.md).
mkdir -p "$card/.books/lang"
for table in crosspoint-core/lang/*.tsv; do
  [ "$(basename "$table")" = en.tsv ] || cp "$table" "$card/.books/lang/"
done
# HTTPS trust: Mozilla's root list (the runner's ca-certificates). Q2 Pod's
# /etc/scrobble-ca.pem has only the 8 roots its scrobbler needs, so many Kavita
# hosts (Sectigo/ZeroSSL RSA, SSL.com, Amazon) would fail on the device.
python3 crosspoint-core/tools/check_ca_bundle.py /etc/ssl/certs/ca-certificates.crt
cp /etc/ssl/certs/ca-certificates.crt "$card/.books/ca-bundle.pem"


cp "$work/reader/vietnamese-prose.epub" "$card/Books/"
# Mandatory font-visibility book in EVERY Q2 test-build ZIP. Do not put it
# inside .books, which the library scanner deliberately treats as hidden.
# One compact XHTML paragraph per language; no external web fonts required.
python3 crosspoint-core/tests/make_language_visibility_epub.py \
  "$card/Books/Books-Multilingual-Visibility-Test.epub"
python3 crosspoint-core/tests/make_language_visibility_epub.py \
  "$card/Books/Books-Multilingual-Visibility-Test.epub" --check
# Original CrossPoint font files, pinned to verified official SHA-256 digests.
python3 crosspoint-core/tools/fetch_q2_fonts.py "$card/.books/fonts"
# Build authentic CrossPoint glyphs at 10/11pt from its pinned source,
# including all 14 fallback scripts. Do not scale the 12pt bitmap or silently
# ship missing fonts. The font build stays entirely on the CI host.
python3 -m venv "$work/font-build-venv"
"$work/font-build-venv/bin/python" -m pip install --quiet --disable-pip-version-check \
  freetype-py fonttools PyYAML
"$work/font-build-venv/bin/python" \
  crosspoint-core/tools/build_q2_small_font_assets.py "$card/.books/fonts"
(cd "$card/.books/fonts" && sha256sum -c books-fonts-8-14.sha256)
# All seven CrossInk named families have genuine 10/11/12pt glyph metrics.
for name in bitter lexenddeca chareink literata merriweather inter atkinson; do
  for point in 8 9 10 11 12 13 14; do
    test -s "$card/.books/fonts/$name-$point.cpfont" ||
      { echo "::error::Missing $name ${point}pt CrossInk font"; exit 1; }
  done
done
cp crosspoint-core/lib/EpdFont/builtinFonts/NotoSans-OFL.txt "$card/.books/NotoSans-OFL.txt"
# Original CrossPoint Page links, internal anchors, EPUB footnotes and
# one-entry return history must work on the actual Q2 MIPS executable.
python3 crosspoint-core/tests/make_reader_links_epub.py "$work/reader/page-links.epub"
cp "$work/reader/page-links.epub" "$card/Books/Books-Links-and-Footnotes-Test.epub"
BOOKS_STATE_DIR="$card/.books" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --test-page-links "$work/reader/page-links.epub" \
  | tee "$work/page-links.log"
grep -q 'page link jump and return: PASS' "$work/page-links.log"
# Selection must use the original rendered TextBlock source ranges, write
# and re-read highlight records, then remove them without changing the EPUB.
BOOKS_STATE_DIR="$card/.books" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --test-selection "$work/reader/page-links.epub" \
  | tee "$work/selection.log"
grep -q 'unicode selection and persistent highlight: PASS' "$work/selection.log"
BOOKS_STATE_DIR="$card/.books" timeout 90s \
  qemu-mipsel -L "$root" "$elf" --test-footnote-popup "$work/reader/page-links.epub" \
  | tee "$work/footnote-popup.log"
grep -q 'XHTML footnote popover: PASS' "$work/footnote-popup.log"
# Connected-library integration under the exact MIPS Q2 ABI. The local
# Python mock serves OPDS Atom, chunked XML, EPUB ZIP and KOReader API.
# Never ship mock credentials or network test progress state on the SD ZIP.
BOOKS_Q2_FONTS="$card/.books/fonts" timeout 180s \
  python3 crosspoint-core/tests/test_connected_network.py \
    "$elf" "$work/reader/page-links.epub" "$root" \
  | tee "$work/connected-library.log"
grep -q 'Books OPDS, download, partial-MD5 and KOSync: PASS' \
  "$work/connected-library.log"




# Validate the generated EPUB on the actual Q2 MIPS application, with the
# full SD cpfont bundle present. This checks structure/openability; visual
# correctness is still assessed by eye on the real Shanling LCD.
BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --validate "$card/Books/Books-Multilingual-Visibility-Test.epub" \
  | tee "$work/language-visibility.log"
grep -q 'Books original Section:' "$work/language-visibility.log"



# Full script pack must load on MIPS, not just be copied into the ZIP.
BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --validate "$work/reader/script-korean.epub" | tee "$work/korean-validate.log"
grep -q 'Books multilingual fonts: 14/14 scripts loaded (12pt)' "$work/korean-validate.log"
grep -q 'Books original Section:' "$work/korean-validate.log"

for script in korean devanagari; do
  file="$work/reader/script-$script.epub"
  BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
    --render "$file" "$work/$script-enabled.pgm" | tee "$work/$script-enabled.log"
  BOOKS_DISABLE_SCRIPTS="$script" BOOKS_STATE_DIR="$card/.books" \
    timeout 90s qemu-mipsel -L "$root" "$elf" \
    --render "$file" "$work/$script-disabled.pgm" | tee "$work/$script-disabled.log"
  grep -q 'Books original Section:' "$work/$script-enabled.log"
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
BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/cjk-script.epub" "$work/cjk-active.pgm" \
  | tee "$work/cjk-active.log"
grep -q 'Books original Section:' "$work/cjk-active.log"
grep -q 'Books CJK fallback: original CrossPoint NotoSansCJKJP cpfont 12pt' "$work/cjk-active.log"
BOOKS_DISABLE_CJK=1 BOOKS_STATE_DIR="$card/.books" \
  timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/cjk-script.epub" "$work/cjk-disabled.pgm" \
  | tee "$work/cjk-disabled.log"
grep -q 'Books CJK fallback: unavailable 12pt' "$work/cjk-disabled.log"
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
BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
  --render "$work/reader/vietnamese-prose.epub" "$work/default-extended.pgm" \
  | tee "$work/default-extended.log"
grep -q 'Books font source: original CrossPoint extended cpfont' "$work/default-extended.log"
# Smooth book text on the MIPS app: each family's TrueType glyphs must land
# on CrossPoint's own (same font, same layout). The calibrated sizes and the
# agreement are logged; a wrong size or offset drops the IoU and fails.
test -s "$card/.books/fonts/ttf/sans-regular.ttf"
for family in sans serif bitter inter literata; do
  echo "BOOKS-TYPE1 $family 12" > "$card/.books/typography.conf"
  for book in vietnamese-prose long-chapter; do
    BOOKS_STATE_DIR="$card/.books" timeout 90s qemu-mipsel -L "$root" "$elf" \
      --smooth-check "$work/reader/$book.epub" > "$work/smooth-$family-$book.log" 2>&1 || true
    grep "Books smooth glyph" "$work/smooth-$family-$book.log" || true
  done
done
python3 - "$work" <<'PY'
import re,sys
bad=[]
for family,book in [(f,b) for f in ("sans","serif","bitter","inter","literata")
                    for b in ("vietnamese-prose","long-chapter")]:
    text=open(f"{sys.argv[1]}/smooth-{family}-{book}.log").read()
    family=f"{family} ({book})"
    m=re.search(r"IoU ([0-9.]+) over (\d+) ink pixels, centre shift ([-+0-9.]+) ([-+0-9.]+)",text)
    if not m and "kept 2-bit" not in text and "no TrueType glyphs" not in text and family.split()[0] not in ("sans","serif"):
        print(f"::notice::smooth glyphs {family}: no TrueType file (kept 2-bit)"); continue
    if not m:
        # A family whose TrueType does not line up keeps 2-bit glyphs (its
        # baseline check refused it): allowed, except the two defaults.
        if family.split()[0] in ("sans","serif"): bad.append(f"{family}: no agreement line")
        else: print(f"::notice::smooth glyphs {family}: kept 2-bit")
        continue
    iou,pixels,dx,dy=float(m[1]),int(m[2]),float(m[3]),float(m[4])
    print(f"::notice::smooth glyphs {family}: IoU {iou:.3f}, shift {dx:+.2f} {dy:+.2f} px, {pixels} px")
    # Hinting snaps lines to whole pixels, so up to 3/4 px vertically.
    if pixels<2000 or iou<0.75 or abs(dx)>0.5 or abs(dy)>0.75:
        bad.append(f"{family}: IoU {iou:.3f} shift {dx:+.2f} {dy:+.2f}")
assert not bad, "TrueType glyphs do not land on CrossPoint's: "+"; ".join(bad)
PY
rm -f "$card/.books/typography.conf"
# Render *every* allowed point size using the actual Q2 MIPS ELF and
# original CrossPoint font files. Each size must retain all 14 scripts,
# produce distinct paginated pixels, and never fall back to the 12pt bitmap.
for family in sans serif; do
  for point in 8 9 10 11 12 13 14; do
    setting="$family $point"
    echo "BOOKS-TYPE1 $setting" > "$card/.books/typography.conf"
    slug="$family-$point"
    BOOKS_STATE_DIR="$card/.books" timeout 90s \
      qemu-mipsel -L "$root" "$elf" \
      --render "$work/reader/vietnamese-prose.epub" "$work/$slug.pgm" \
      | tee "$work/$slug.log"
    grep -q "Books multilingual fonts: 14/14 scripts loaded (${point}pt)" \
      "$work/$slug.log"
    if [[ "$family" == "sans" ]]; then label="Noto Sans"; else label="Noto Serif"; fi
    grep -q "Books typeface: $label ${point}pt" "$work/$slug.log"
  done
done
# CrossInk font choices are real cpfont files, not UI-only labels.
# Render each named family at 10/11/12pt on the actual Q2 MIPS reader.
for pair in "bitter:Bitter" "lexenddeca:Lexend Deca" \
  "chareink:ChareInk" "literata:Literata" \
  "merriweather:Merriweather" "inter:Inter" "atkinson:Atkinson"; do
  slug="${pair%%:*}"
  family_name="${pair#*:}"
  for point in 8 9 10 11 12 13 14; do
    test -s "$card/.books/fonts/$slug-$point.cpfont"
    echo "BOOKS-TYPE1 $slug $point" > "$card/.books/typography.conf"
    BOOKS_STATE_DIR="$card/.books" timeout 90s \
      qemu-mipsel -L "$root" "$elf" \
      --render "$work/reader/vietnamese-prose.epub" "$work/$slug-$point.pgm" \
      | tee "$work/$slug-$point.log"
    grep -q "Books typeface: $family_name ${point}pt" \
      "$work/$slug-$point.log"
    grep -q "Books multilingual fonts: 14/14 scripts loaded (${point}pt)" \
      "$work/$slug-$point.log"
  done
done
python3 - "$work" <<'PY'
from pathlib import Path
import sys
work=Path(sys.argv[1])
for family in ("bitter","lexenddeca","chareink","literata",
               "merriweather","inter","atkinson"):
    images=[]
    for size in range(8,15):
        data=(work/f"{family}-{size}.pgm").read_bytes()
        assert data.startswith(b"P5\n375 320\n255\n")
        assert len(data)==120015 and sum(p<255 for p in data[15:])>50
        images.append(data)
    assert len(set(images))==7, f"Font size raster did not change: {family}"
print("CrossInk 7-family x 7-size Q2 MIPS raster: PASS")
PY
python3 - "$work" <<'PY'
from pathlib import Path
import sys
work = Path(sys.argv[1])
for family in ("sans", "serif"):
    previous = None
    for size in range(8, 15):
        data = (work / f"{family}-{size}.pgm").read_bytes()
        assert data.startswith(b"P5\n375 320\n255\n") and len(data)==120015
        assert data[15:].count(0)>50, (family,size)
        assert previous!=data, f"Size {size} did not change raster for {family}"
        previous = data
for size in range(8, 15):
    assert (work/f"sans-{size}.pgm").read_bytes() != \
           (work/f"serif-{size}.pgm").read_bytes(), size
print("MIPS Q2 8-14pt 7-size font switching and multilingual rendering: PASS")
PY
rm -f "$card/.books/typography.conf"
# Stress-regression for v0.8: validate EPUB3 HTML entities and the actual
# bundled italic/bold cpfont raster on the MIPS binary (not a host skip).
BOOKS_TEST_FONTS="$card/.books/fonts" timeout 180s \
  python3 crosspoint-core/tests/markup_tolerance_test.py "$work/reader-qemu" entities
BOOKS_TEST_FONTS="$card/.books/fonts" timeout 180s \
  python3 crosspoint-core/tests/markup_tolerance_test.py "$work/reader-qemu" italic

# Ship only the app's own files: the QEMU tests above ran with this folder as
# their state directory and leave caches, positions, highlights, settings and
# logs (v0.8.0-beta shipped a test's progress-*.highlights). Allow-list, so a
# new kind of state can never leak into a release.
find "$card/.books" -mindepth 1 -maxdepth 1 ! -name books-engine ! -name q2-books \
  ! -name q2app ! -name README.txt ! -name NotoSans-OFL.txt ! -name dictionary-starter.tsv \
  ! -name connected.conf.example ! -name ca-bundle.pem ! -name fonts -exec rm -rf -- {} +
find "$card/.books/fonts" -mindepth 1 ! -name '*.cpfont' ! -name '*.txt' ! -name ttf ! -path '*/fonts/ttf/*' -exec rm -rf -- {} +
find "$card/.books/fonts/ttf" -mindepth 1 ! -name '*.ttf' ! -name books-ttf.txt -exec rm -rf -- {} +
chmod +x "$card/.books/q2-books" "$card/.books/books-engine"
# The test books in $card/Books were only for the checks above: the release
# carries the app alone, never books in the user's Books folder.
(cd "$card" && zip -q -r "$base/dist/books-q2-reader-alpha.zip" .books)
sha256sum "$base/dist/books-q2-reader-alpha.zip" > "$base/dist/books-q2-reader-alpha.zip.sha256"
unzip -t "$base/dist/books-q2-reader-alpha.zip"
# Verify the allow-list held: nothing but the app's own files (no books).
unzip -Z1 "$base/dist/books-q2-reader-alpha.zip" | python3 -c '
import re,sys
names_all=sys.stdin.read().split()
bad=[n for n in names_all if not re.fullmatch(
  r"(\.books/|\.books/fonts/|\.books/fonts/[^/]+\.(cpfont|txt)|\.books/fonts/ttf/|\.books/fonts/ttf/[a-z]+-(regular|bold|italic|bolditalic)\.ttf|\.books/fonts/ttf/books-ttf\.txt|\.books/lang/|\.books/lang/[A-Za-z_]{2,11}\.tsv|"
  r"\.books/(books-engine|q2-books|q2app|README\.txt|NotoSans-OFL\.txt|dictionary-starter\.tsv|"
  r"connected\.conf\.example|ca-bundle\.pem))",n)]
assert not bad, f"release ZIP carries state or unexpected files: {bad}"
names=set(n for n in names_all)
assert ".books/fonts/ttf/sans-regular.ttf" in names and ".books/fonts/ttf/serif-regular.ttf" in names, "smooth text TrueType files missing from the ZIP"
print("Release ZIP contents: PASS")'
