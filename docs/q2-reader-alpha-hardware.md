# CrossPod Q2 reader alpha — text-only EPUB hardware prototype

**Status:** experimental. Compiled for MIPS32 little-endian Linux with static
zlib/Expat/glibc/libstdc++. QEMU EPUB rendering and bidirectional pagination validated. **NOT validated on Shanling Q2 yet.**
**Do not flash firmware or overwrite the stock Books app.**

The bundled executable `.crosspod/crosspod` is the first runnable EPUB text
reader using original CrossPoint ZIP/OPF/spine, ParsedText, TextBlock and
GfxRenderer. It is substantially more than the earlier static page demo,
but NOT the complete original CrossPoint EPUB presentation engine.

## Supported and missing features

- Supported: zipped EPUB, original container.xml + OPF spine order, a
  bounded XHTML chapter extractor, Vietnamese text, per-chapter pagination,
  Q2 LCD framebuffer, wheel next/previous, centre/next and Back exit,
  left/right half touchscreen page navigation through the Q2 socket.
- NOT implemented: full original ChapterHtmlSlimParser, cascade of book CSS,
  images/inline floats, complex glyph fallback, CJK/Arabic/Hebrew fonts,
  hyperlinks, footnotes, TOC UI, font-size menu, bookmarks, reading-position
  persistence, suspend/resume policy.
- XHTML with unusually large or malformed chapters is refused, not processed
  without bounds. Some layouts will be skipped or lose structure.

## Manual first test (shell/SSH with recovery access)

1. Unzip this package on your PC. Copy its `.crosspod/crosspod` file to a
   TEMPORARY SD-card directory (for example `/mnt/mmc/crosspod-alpha/`) rather
   than replacing the stock reader or your existing CrossPod install.
2. Copy `sample-books/vietnamese-prose.epub` to the Q2 SD card, e.g.
   `/mnt/mmc/Books/vietnamese-prose.epub`.
3. In a Q2 shell, verify the executable and ebook:

   ```sh
   cd /mnt/mmc/crosspod-alpha
   chmod +x crosspod
   ./crosspod --validate /mnt/mmc/Books/vietnamese-prose.epub
   ```

   **`--validate` never opens the LCD or the input socket.** If this fails
   with "not found", illegal instruction, permission denied, or library
   problems, STOP and report `uname -a`, `file crosspod`, and the error.
   No OS libraries or firmware need to be replaced.

4. To test the screen interactively, first ensure the existing Q2 Pod Books
   hook forwards input to `/tmp/q2-crosspod.input.sock` and stops drawing
   while the reader runs. The hook currently exists only in the Q2 Pod fork
   integration, not necessarily in your installed firmware. A stock Q2 Pod
   installation may not provide it. **Do not run the display mode while
   another application draws to the framebuffer**.
5. With active SSH and a way to restart the Q2 Pod display, launch:

   ```sh
   timeout 120 /mnt/mmc/crosspod-alpha/crosspod /mnt/mmc/Books/vietnamese-prose.epub
   ```

   Scroll using the click wheel; use the Back key to exit. Timeout (if your
   BusyBox supplies it) adds a fallback exit. Closing the process releases the
   framebuffer and input socket, but no hardware restoration has been proved.

## How the binary is built

GitHub Actions compiles pinned zlib 1.3.1 and Expat 2.6.4 from source using
MIPS GCC, statically links the CrossPoint engine, and exercises the resulting
ELF in `qemu-mipsel` with an EPUB spine test and real Vietnamese rendering.
No ESP32 firmware, bootloader or firmware update image is included.

**Important architecture difference:** this CI binary uses a generic
MIPS32r2 Linux toolchain, not the Q2-specific sysroot and Nan2008 ABI
settings. Hardware compatibility and speed are still unverified.
Build with the device's verified Q2 sysroot before considering a general beta.

For developers: draft PR #15 tracks the complete CrossPoint port.

## Extra headless validation

Before starting the LCD, exercise both forward and reverse page transitions:

```sh
./crosspod --walk /mnt/mmc/Books/vietnamese-prose.epub
```

This runs the *actual MIPS reader engine* without opening the framebuffer.
The fork's [Q2 Pod Books integration](https://github.com/lampsonlnguyen/q2-pod/pull/1)
is merged to the fork's main branch, but the installed firmware must contain
that change for the library to launch this executable and forward wheel events.
