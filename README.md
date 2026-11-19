# CrossPod — native ebook reader for Q2 Pod

Experimental native **Shanling Q2 MIPS/Linux** reading app, inspired by [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

> **Status: early TXT prototype, not a CrossPoint EPUB port.** EPUB, chapters, OPDS, bookmarks, and CrossPoint interoperability are NOT implemented. This initial version establishes a real Q2 card-app reader, package, and hardware test loop. Device operation has not yet been confirmed.

## Installation / build

On Ubuntu or WSL install `clang`, `lld`, `make`, and `zip` then run `make`. Extract `dist/crosspod.zip` **at the root of the Q2 microSD card**.

Expected paths:

```text
/.crosspod/q2app
/.crosspod/crosspod
/.crosspod/book.txt
```

Place a **plain ASCII/English UTF-8 TXT file** at `/.crosspod/book.txt` (the build includes a sample). CrossPod currently reads the first 256 KiB of one book and replaces non-ASCII UTF-8 bytes with question marks; it is **not suitable for Vietnamese text yet**. Restart Q2 Pod and open **Library → CrossPod**. Depending on firmware app enumeration, temporarily remove unused card apps if CrossPod does not appear.

## Controls

- Scroll wheel, previous and next buttons: previous/next page
- Center button: next page
- Back: exit CrossPod
- Touch: tap left or right half to turn page

The device displays 375 × 320 pixels in landscape. A software backbuffer is used; framebuffer panning is not used because it was unreliable on other Q2 apps.

## Development roadmap

1. **This commit:** native Linux/MIPS runtime, input, readable pages, TXT loading and ZIP distribution.
2. Add SD library browsing, per-book progress persistence, Unicode fonts (including Vietnamese).
3. Port CrossPoint's EPUB metadata, ZIP streaming, XHTML/CSS pagination and TOC in stages; use its `lib/Epub` as reference, isolating Arduino/FreeInk interfaces from portable code.
4. Optional OPDS and KOReader sync; only after local reading is stable.

CrossPoint's activity controller and renderer are tightly coupled to `freeink-sdk`, ESP32/Arduino APIs, and e-ink. Do **not** attempt to flash CrossPoint's ESP32 `.bin` to the Q2; this is a native Linux card app.

## Attribution

CrossPoint Reader is MIT-licensed for its original code; some included components have separate licenses, and CrossPoint's combined firmware has GPLv3 obligations. This prototype does **not** copy CrossPoint's EPUB implementation. The Q2 framebuffer/input runtime and font are adapted from [q2-games](https://github.com/lampsonlnguyen/q2-games) / [q2-arcade](https://github.com/lampsonlnguyen/q2-arcade), GPL-3.0-or-later. See `LICENSE`.

**Hardware validation required:** build success does not imply confirmed on-device rendering, controls, or storage paths.
