# CrossPod — TXT + EPUB reader for Q2 Pod

CrossPod is a lightweight, native **Linux/MIPS book reader** for [DiamondBond's Q2 Pod](https://github.com/DiamondBond/q2-pod) on the Shanling Q2. Its goal is to adapt reading ideas and portable components from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) to the Q2, without running ESP32 firmware or replacing Q2 Pod.

**Development status:** native TXT reader and experimental EPUB ZIP/DEFLATE + XHTML text extraction have been implemented. CI and physical Q2 performance **have not yet been verified**. This is not a full CrossPoint EPUB layout engine.

## Book formats

| Format | Current support |
| --- | --- |
| `.txt` | Basic plaintext display and page navigation |
| `.epub` | Unencrypted EPUB/ZIP using stored or DEFLATE-compressed `.html` / `.xhtml` chapters; simplified text-only extraction |
| EPUB images, CSS, rich layout, TOC | Not yet |
| DRM-locked EPUB | Not supported |
| Vietnamese precomposed UTF-8 (NFC) | Experimental bitmap glyph rendering and codepoint-aware pagination (not yet hardware validated) |

The current EPUB parser does **not** yet use the OPF spine, so chapters are displayed in ZIP entry order (which can differ from reading order). Some valid EPUBs will not render correctly. Individual compressed XHTML files must be no larger than 512 KiB and uncompressed XHTML files no larger than 256 KiB. The extracted reading text is limited to 256 KiB. ZIP64 and encrypted entries are not supported.

## Build and install

On Ubuntu / WSL:

```sh
sudo apt-get install clang lld make zip python3-pil fonts-dejavu-core
git clone https://github.com/lampsonlnguyen/q2-crosspoint
cd q2-crosspoint
make
```

Extract `dist/crosspod.zip` at the **SD card root**. The package contains only the app files:

```text
/.crosspod/q2app
/.crosspod/q2-crosspod
```

Place TXT and unencrypted EPUB books in the existing **`Books/`** folder at the **root of the SD card**. No renaming is required:

```text
SD Card/
  .crosspod/
    q2app
    q2-crosspod
  Books/
    Dracula.epub
    My Notes.txt
    Fiction/
      Novel.epub
```

CrossPod opens a simple library picker on launch. Use the wheel or previous/next buttons to select a book, then press **Center**. Touch a row to select; tap it again to open it. Press **Back** in the reader to return to the list, then Back once more to exit. Warm/cool tone and night mode remain available while reading.

**Limits:** up to 128 books indexed, subfolders up to three levels deep (the root `Books/` level is depth zero), and relative filenames under 192 bytes. Results are sorted by filename/path. Hidden files and symbolic links are skipped. EPUB layout remains experimental and text-only, and book selection does not yet save individual reading positions.

Restart Q2 Pod, then look for **CrossPod directly in Library**, not Games. Q2 Pod currently does not expose a manifest setting to nest card apps under **Library → Books**; that requires a firmware change. If CrossPod is not discovered, note Q2 Pod's card-app discovery limit and temporarily remove unused apps.

## Vietnamese text status

CrossPod previously drew only ASCII and displayed non-ASCII UTF-8 bytes as question marks. The new reader renderer decodes Unicode codepoints, uses generated grayscale glyph bitmaps from DejaVu Sans, and keeps multi-byte characters intact while paginating. This covers common **precomposed Vietnamese (NFC)**, including `đ ư ơ ă â ê ô` and tonal variants in text and filenames. NFD combining accents are drawn on the preceding glyph but do not yet have full normalization/shaping or reliable mark positioning. EPUB XHTML decoding is still incomplete; HTML entities beyond a small basic subset are not supported. These fixes are not yet confirmed on Q2 hardware.

**Building needs `python3-pil` and `fonts-dejavu-core`.** The font is rasterized once at build time to `src/font_vn.h`; there is no Python, FreeType, or dynamic font engine in the Q2 executable. A full CrossPoint font/EPUB engine port has **not** happened.

## Reading tone and night mode

CrossPod now has a software-based **warm/cool reading tone** (0 cool to 100 warm) and a separate **night mode**. While reading, tap **TONE** at the upper-right corner. Use the **wheel** or touch the color strip to adjust warmth. Tap **NIGHT**, or press the center button while the tone panel is open, to toggle dark reading mode. Press Back to close the panel. The preferences are saved in `/.crosspod/tone.cfg` and restored at next launch.

This changes only CrossPod's rendered page palette, **not** the Q2 LCD's hardware backlight temperature or brightness. No continuous visual effects or background redraws are used: a finished page is drawn only when needed.

## Controls

- **Book list:** wheel or previous/next to select, Center to open, Back to exit
- **Reading:** wheel or previous/next to change pages, Center for next page, Back to book list
- **Touch:** select/open rows in book list; left/right half to turn pages while reading

## Design

The reader uses the native Q2 framebuffer, a lightweight software-drawn paper-style page, and the existing Q2 card-app input socket approach. Unlike a game, redraw is only requested when changing pages, reducing background work.

Core files:

- `src/platform.c` — MIPS system calls, framebuffer, input and file IO
- `src/reader.c` — TXT indexing, pagination, navigation
- `src/epub.c` — EPUB ZIP directory lookup and XHTML-to-text conversion
- `src/sinfl.h` — small independent DEFLATE decoder by Micha Mettke
- `src/render.c` — plain book-page UI
- `tests/test_reader.c` — portable TXT/EPUB parser smoke test

## Next milestones

1. Validate CI compilation and real device reading, including nested SD directories.
2. Save per-book reading positions and optionally resume the last open book.
3. Correct EPUB OPF manifest and spine ordering, efficient chapter paging and CSS-aware layout.
4. Improve Vietnamese NFD combining-mark positioning, word wrapping, and user-selectable fonts.
5. Optional TOC, bookmarks and KOReader progress sync.

## Licensing and credits

CrossPod's application code and Q2 bridge derive from the GPL-3.0-or-later native Q2 examples in [q2-games](https://github.com/lampsonlnguyen/q2-games), and are distributed under GPL-3.0. The adapted font originated from that Q2 example and retains its original licensing obligations. `src/sinfl.h` is Micha Mettke's MIT/public-domain code, with notice retained in the file. CrossPoint Reader itself is separately maintained; its original code is generally MIT-licensed, while some components have additional terms. No claim of official CrossPoint affiliation or full CrossPoint feature compatibility is made.
