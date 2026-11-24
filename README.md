# CrossPod — a port of CrossPoint Reader for Shanling Q2

**CrossPod** a.k.a. as Q2 Books, is an ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod)
on the Shanling Q2: a port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)'s
EPUB engine, layout and fonts to the Q2's Linux. It appears in Q2 Pod as **Books**
(card folder `.books`, program `.books/q2-books`). Earlier releases were named
differently; the first start of this version copies your reading positions,
bookmarks, highlights and settings from the old card folder into `.books`.

**Status:** 1.1.1 is the current release (1.0.0 was the first under the name Books)
(earlier releases were v0.3–v0.9 as CrossPod). It passes the host test suites
(GCC, Clang, ASan), EPUB fuzzing and the Q2 MIPS/QEMU build, but it has **not
yet been validated on a physical Q2**: see M1 below.
User notes: [v0.10–v0.11](docs/v0.10-v0.11-reading-dictionaries-opds.md),
[release notes](docs/release-notes/v1.1.1.md).

## Milestones

Finished: M0 HTTPS transport (0.9), M2 reader essentials (0.10), M3 dictionary, fonts and languages (0.11; menus follow Q2 Pod's language since 1.1.2) and M4 library and servers (1.1.2; see [the M4 guide](docs/v0.12-library-and-servers.md)). Their remaining follow-ups are listed below.

## What still needs doing

**On a physical Q2 (M1, the release gate for "stable")**
- Fill a hardware QA matrix: firmware revision, release ZIP SHA-256, SD card type, pass/fail with photos and sanitized logs.
- Launch from Library; wheel at 10/50/100/200 % sensitivity; fast turns, Back, Centre and touch; auto page turn with and without Smart Awake.
- Screen off/on and Q2 Pod suspend; resume at the exact paragraph; forced exit during layout and downloads.
- SD card full, read-only, missing, unplugged and replugged; USB storage mode while running.
- Slow or missing Wi-Fi; an HTTPS Kavita server: browse, download, push/pull progress, refuse an invalid certificate with a readable error.
- Idle and active RAM, CPU, SD writes, battery drain and heat over a 30-minute session.
- Upgrade from CrossPod: check the one-time copy of positions, bookmarks and settings from `.crosspod` into `.books`.

**Reader and languages (from M3)**
- Native-speaker review of the [menu translations](crosspoint-core/lang/README.md) and additional Books-specific labels for Japanese, Korean, Thai and Chinese.
- Reading TTF/OTF fonts directly: only after a memory benchmark on the Q2 (today `.cpfont` files in `Fonts/`).

**Library and servers (from M4)**
- Syncing highlights: KOReader sync stores only a position; Kavita 0.8.8+ keeps highlights in its own REST API (its own sign-in, chapter ids and format), not used yet.
- Measure the 4000-book scan and folder browsing on a Q2's SD card.

**EPUB fidelity (M5)**
- Compare the corpus with upstream CrossPoint pixel for pixel, using the same settings, and document the allowed differences (ruby and tables are not laid out as upstream will show them).
- Extend coverage from public-domain excerpts to full-length books with tables, ruby, CJK and Indic text.
- Measure the M5 parse change on a Q2: page-turn time and RAM while a chapter is laid out.

**Polish (M6)**
- Measure page-turn time and battery with smooth text on a Q2 (each new page is drawn five times); add a Reading settings switch for it.
- The clock is 24-hour only: Q2 Pod's 12/24-hour setting is not readable by a card app.
- Refresh the menu bar's clock while a menu stays open; time left in the chapter.

**Release and packaging (M7)**
- Compatibility and test report from the device, a troubleshooting guide and a user manual.
- Font and dependency license audit (the bundled font licenses are collected in `.books/fonts/CrossPoint-font-licenses.txt`; a full audit is pending).
- Q2 Pod's App Store: upstream must take `books` off its reserved ids (`patch/netupdate.c`) and enable `lampsonlnguyen/q2-books` in `sources.list`; until then, install by extracting the release ZIP to the SD card.
- Q2 Pod's built-in Books entry and this app both appear in the Library until Q2 Pod hands the name over.
- Remove the one-time `.crosspod` migration in a later release, once upgrades are done.

**Known limits today**
- A server certificate with a CN and no subjectAltName may be accepted, depending on the Q2's libcurl.
- Battery, suspend and long-session memory are unmeasured.

**Historical note:** The detailed porting inventory below was written during
the early CrossPoint source migration. Its "not yet done" and host-only notes
describe those earlier milestones, not the current state. Refer to the
status above, [the connected library guide](docs/v0.7.0-connected-library.md)
and the v0.8 verification plan for current capabilities and release
requirements.

The earlier Books prototype has been replaced on `main` by the CrossPoint reader-port foundation. The old prototype remains available in Git history; this implementation does not use its custom EPUB extractor or rendering engine.

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
- Original MiniBidi implementation and Arabic/Hebrew visual reordering plus contextual Arabic shaping, compiled and tested on Linux
- Original `ParsedText.cpp` and `Page.cpp` **compile-checked** on Linux (not yet linked as a functioning reader)
- Original `ChapterHtmlSlimParser.cpp` (EPUB XHTML/CSS chapter parsing) **compile-checked** on Linux with original `Epub.h`; section and image-decoder source headers staged, but not yet linked as a functioning reader
- Q2 logical-screen `HalDisplay` with original CrossPoint white/ink 1-bpp convention, frame-publication bridge and a tested raster path
- Q2 Linux socket input, native double-buffered framebuffer and page-presentation adapter; these are individually tested, not wired to a complete reader
- Original disk-backed `BookMetadataCache` with buffered serialization and persistent `book.bin` index, reducing spine RAM usage
- A Linux `books-epub-check` command for testing EPUB archive/container integrity without claiming to render pages
- Standalone Q2 Pod Books shelf handoff contract for `/mnt/mmc/Books/...` paths and Vietnamese filenames
- A standalone click-wheel and touch-event translator independent of the Q2 Pod card-app framework
- A basic LCD output adapter for CrossPoint-style monochrome rasters and optional warm/cool palettes
- A sparse checkout of CrossPoint's **reader-related source directories only** (no ESP32 firmware build)
- CMake host compilation and tests for Unicode, TXT conversion, EPUB archive/ZIP streaming, container XML, wheel/touch input, and LCD output adapters

**Not yet done:** End-to-end chapter XHTML/CSS pagination, linking `ParsedText` and `Page` to the full upstream `GfxRenderer`/font loading/shaping and reader activities, then device-level CPU/input/display/power verification on Q2. The Q2 display/transport interfaces exist but are not driven by a full page engine. There is no installable Books reader ZIP yet. A genuine build requires implementing Linux-compatible adapters for CrossPoint's reader and FreeInk interfaces; no ESP32 firmware components will be built or shipped.

## Build currently ported source tests

```bash
sudo apt-get update && sudo apt-get install -y git g++ cmake make libexpat1-dev zlib1g-dev python3
git clone https://github.com/lampsonlnguyen/q2-crosspoint.git
cd q2-crosspoint
make
```

`make` runs upstream CrossPoint parsing/cache and Q2 adapter tests and builds a **host-only EPUB validation command**, `build/crosspoint-core/books-epub-check`. This does not produce an installable Q2 reader.

To inspect only the reader-related CrossPoint source at the pinned revision:

```bash
make upstream
```

A sparse checkout appears in `crosspoint-core/upstream/` and is intentionally ignored by Git. Only the Unicode, TXT, EPUB, fonts, rendering and reader-activity source families are checked out; this does not compile any ESP32 firmware.

## Q2 Pod Books integration

Q2 Pod already has a `/Books` shelf and a basic EPUB/TXT reader in `patch/books.c`. Its EPUB implementation flattens XHTML into plain text and paints it using the stock font. Books will **keep the Books shelf** but replace the reading experience with CrossPoint's actual engine in an independent Linux process. For the source-level findings, planned controls and safe fallback details, see [Q2 Pod Books integration](docs/q2pod-books-integration.md).

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
