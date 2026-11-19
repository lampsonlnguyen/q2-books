# Shanling Q2 MIPS hardware diagnostic

**Not an EPUB reader and not a firmware updater.** The host platform adapter
is compiled for MIPS32 Linux by GitHub Actions and attached as a diagnostic
artifact. The complete CrossPoint layout/font/render engine is still not
linked into a reader executable.

## Cross-build

```sh
sudo apt install cmake g++-mipsel-linux-gnu binutils-mipsel-linux-gnu
cmake -S crosspoint-core/platform/q2 -B build/q2-mips \
    -DCMAKE_CXX_COMPILER=mipsel-linux-gnu-g++ \
    -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=mipsel
cmake --build build/q2-mips -j2
mipsel-linux-gnu-readelf -h build/q2-mips/crosspod-hwcheck
```

The Ubuntu cross toolchain does **not** ship Q2 Pod's original runtime
libraries. A successful MIPS compile only proves the instruction set/ELF
target, **not** glibc or libstdc++ compatibility with the Q2. For a real
device test, rebuild with the known-good Q2 Linux sysroot, or audit dynamic
dependencies first:

```sh
mipsel-linux-gnu-readelf -l build/q2-mips/crosspod-hwcheck
mipsel-linux-gnu-readelf -V build/q2-mips/crosspod-hwcheck
```

## Device test gates

A developer with Q2 hardware and a working remote shell should:

1. Verify the binary's interpreter, architecture, glibc and libstdc++
   versions against the **device** before executing. Never replace
   `/usr/bin/demo`, firmware, or the stock Books reader to test.
2. Copy the diagnostic to a temporary directory, e.g.
   `/mnt/mmc/.crosspod-test/crosspod-hwcheck`; set executable permission.
3. Run `crosspod-hwcheck` *without* `--display` to test Unix datagram
   input independently; it creates `/tmp/q2-crosspod.input.sock` for up
   to 30 seconds, prints parsed events and closes on Return key up (170).
   To send one synthetic message, use a tiny AF_UNIX SOCK_DGRAM Python
   client, or the Q2 Pod Books launcher when its process owns the socket.
4. Only with Q2 Pod framebuffer painting suspended and a recovery shell
   ready, run `crosspod-hwcheck --display`. Check color bars orientation,
   double-buffer panning, no tearing, and restored screen after exit.
   The diagnostic closes automatically within 30 seconds.
5. Measure idle CPU and resident RSS during the run, verify exit behavior,
   and save logs/screenshots. This must be completed on real hardware; no
   firmware display/input compatibility has been confirmed through CI.

The separate firmware Books hook already targets
`/mnt/mmc/.crosspod/crosspod`, **not** `crosspod-hwcheck`; the diagnostic
cannot be mistaken for an installable reader.

## Follow-up for complete engine

CrossPoint's original `ZipFile`, `ContainerParser`, `ContentOpfParser`,
`BookMetadataCache`, `CssParser` and `WordStore` have POSIX/host build
coverage. The full `ParsedText`, `Page`, `EpdFont`, `GfxRenderer` and
reader activity code must be adapted together, preserving multilingual layout,
embedded styles/images and the original rendering path. The output should
drive `Q2PagePresenter` through `Q2SocketInput` in a real reader process.
Only then package `crosspod` as the Books integration target.
