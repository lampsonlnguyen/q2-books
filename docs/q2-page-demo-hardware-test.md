# Books Q2 Vietnamese page — controlled first hardware test

**This is NOT the ebook reader.** The bundle shows one original CrossPoint
rendered Vietnamese EPUB page on the Q2 LCD. No flashing or firmware changes.

## Preconditions
- Access to a shell on the Q2 (SSH/q2remote).
- USB/SSH access remains available to recover if the screen becomes blank.
- Current Q2 Pod screen painting must be suspended **before** `--display`.
- The included executable is **experimental** MIPS32 little-endian Linux,
  cross-compiled with Ubuntu GCC. Device glibc and libstdc++ compatibility
  has NOT been verified; if the loader rejects it, do not replace OS libraries.
- Make a backup. Do not install over the stock Books reader, firmware or
  `/mnt/mmc/.books/books`.

## Prepare safely

Copy the extracted `q2-hardware-test` directory to the Q2's SD card at
`/mnt/mmc/q2-hardware-test` (verify its mount path first). In a Q2 shell:

```sh
cd /mnt/mmc/q2-hardware-test
sha256sum -c SHA256SUMS
chmod +x books-page-demo
./books-page-demo --check books-vietnamese-preview.pgm
```

The `--check` mode does **not open /dev/fb0**. Check that it prints
`Books page verified`. If you see `not found` or a missing runtime
interpreter/library, stop here and collect:

```sh
uname -a
ls -l /lib/ld* /usr/lib/libstdc++* 2>/dev/null
cat /proc/fb
```

Do **not** copy replacement system libraries onto the Q2.

## Display (only once Q2 Pod has released the framebuffer)

With a working recovery shell, stop/suspend Q2 Pod's active framebuffer
painting using your existing device-specific method. Then:

```sh
cd /mnt/mmc/q2-hardware-test
./books-page-demo --display books-vietnamese-preview.pgm
```

The executable checks framebuffer geometry before any drawing. It displays
the static CrossPoint-rendered Vietnamese page for at most **20 seconds**,
then attempts to restore the original framebuffer page. Ctrl-C exits sooner.
On error it exits nonzero. Resume the Q2 Pod screen painter afterward.

Test that Vietnamese accents are legible, text does not tear, the page is
oriented correctly, and Q2 Pod resumes normally. Collect `dmesg` if possible.

**Caveat:** this tests the CrossPoint host-rendered page being presented through
the real Q2 Linux framebuffer. It does NOT execute the full EPUB parsing,
font rasterization, page navigation, or text layout on the Q2 CPU.

The Q2 MIPS binary was checked by `qemu-mipsel` in the CI pipeline using
`--check`, but on-device execution has not yet been established.
