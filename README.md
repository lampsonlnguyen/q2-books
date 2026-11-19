# CrossPod — direct CrossPoint Reader port to Shanling Q2

**This repository contains an incomplete CrossPoint engine port. It is not yet a functional Q2 e-reader.**

The earlier CrossPod prototype has been replaced on `main` by the CrossPoint reader-port foundation. The old prototype remains available in Git history; this implementation does not use its custom EPUB extractor or rendering engine.

The new architecture retains CrossPoint's source-level EPUB/TXT parsing, layout, fonts, page caches, activities and settings, and adapts only the hardware interfaces to the Q2's Linux display, click wheel, touchscreen and SD card. See [the technical plan](crosspoint-core/README.md).

## What is actually ported now

- Original CrossPoint Unicode/NFC code, unmodified and with original license
- Original CrossPoint TXT-to-XHTML converter, unmodified and compiled with small POSIX compatibility shims
- Original CrossPoint EPUB `container.xml` parser, built with Linux Expat, rather than Q2 Pod's plain-text EPUB converter
- Original CrossPoint `ZipFile` EPUB archive handling, compiled unchanged with POSIX file I/O and streaming zlib DEFLATE adapters; regression fixture includes Vietnamese, CJK, Arabic, Hebrew, Thai, Indic, Cyrillic and Greek text
- Original CrossPoint `ContentOpfParser` for EPUB metadata, manifest and spine order
- Original EPUB `CssParser` and disk-efficient `WordStore` (styling and bounded page-layout word storage)
- Original `EpdFont`/`EpdFontFamily` Unicode coverage and glyph metrics, with original borrowed-code license notices
- Original CJK punctuation line-breaking, Hangul boundary rules, Unicode hyphenation helpers and token-break primitives
- Q2 Linux socket input, native double-buffered framebuffer and page-presentation adapter; these are individually tested, not wired to a complete reader
- Original disk-backed `BookMetadataCache` with buffered serialization and persistent `book.bin` index, reducing spine RAM usage
- A Linux `crosspod-epub-check` command for testing EPUB archive/container integrity without claiming to render pages
- Standalone Q2 Pod Books shelf handoff contract for `/mnt/mmc/Books/...` paths and Vietnamese filenames
- A standalone click-wheel and touch-event translator independent of the Q2 Pod card-app framework
- A basic LCD output adapter for CrossPoint-style monochrome rasters and optional warm/cool palettes
- A sparse checkout of CrossPoint's **reader-related source directories only** (no ESP32 firmware build)
- CMake host compilation and tests for Unicode, TXT conversion, EPUB archive/ZIP streaming, container XML, wheel/touch input, and LCD output adapters

**Not yet done:** CrossPoint EPUB chapter HTML/CSS layout, reader activities, font system, GfxRenderer, LCD I/O and power HAL on MIPS Linux. There is no installable CrossPod reader ZIP yet. A genuine build requires implementing Linux-compatible adapters for CrossPoint's reader and FreeInk interfaces; no ESP32 firmware components will be built or shipped.

## Build currently ported source tests

```bash
sudo apt-get update && sudo apt-get install -y git g++ cmake make libexpat1-dev zlib1g-dev python3
git clone https://github.com/lampsonlnguyen/q2-crosspoint.git
cd q2-crosspoint
make
```

`make` runs upstream CrossPoint parsing/cache and Q2 adapter tests and builds a **host-only EPUB validation command**, `build/crosspoint-core/crosspod-epub-check`. This does not produce an installable Q2 reader.

To inspect only the reader-related CrossPoint source at the pinned revision:

```bash
make upstream
```

A sparse checkout appears in `crosspoint-core/upstream/` and is intentionally ignored by Git. Only the Unicode, TXT, EPUB, fonts, rendering and reader-activity source families are checked out; this does not compile any ESP32 firmware.

## Q2 Pod Books integration

Q2 Pod already has a `/Books` shelf and a basic EPUB/TXT reader in `patch/books.c`. Its EPUB implementation flattens XHTML into plain text and paints it using the stock font. CrossPod will **keep the Books shelf** but replace the reading experience with CrossPoint's actual engine in an independent Linux process. For the source-level findings, planned controls and safe fallback details, see [Q2 Pod Books integration](docs/q2pod-books-integration.md).

## Device integration design

- Click wheel: previous/next scrolling/page turning via CrossPoint's input action model
- Touch: gesture and press events using CrossPoint's UI and reader menu
- Display: CrossPoint's layout and rasterization → adapted LCD output
- Storage: standard POSIX files under `/mnt/mmc/Books` with original CrossPoint cache format
- Reading preferences: CrossPoint's settings, with Q2-specific color/brightness controls
- System lifecycle: Q2 Linux idle dim, sleep, restore; not an ESP32 deep-sleep implementation

Q2 Pod, if installed, may offer a Library launcher but must not be used as the e-reader runtime or rendering framework.

## Licensing

The code copied unmodified from CrossPoint is governed by CrossPoint's original MIT license and associated third-party notices: see [LICENSE-CROSSPOINT](crosspoint-core/LICENSE-CROSSPOINT). Check every third-party dependency's license before distribution.

### Q2 MIPS / hardware diagnostic

The `crosspod-hwcheck` diagnostic was merged from [PR #7](https://github.com/lampsonlnguyen/q2-crosspoint/pull/7). Its MIPS cross-build, ELF check and QEMU command-line smoke test passed, but it has **not been tested on the actual Q2 device**. The diagnostic is **not an ebook reader**; physical-device behavior and runtime compatibility are still unverified. See [MIPS hardware test gates](docs/q2-mips-hardware-validation.md) once that PR merges.
