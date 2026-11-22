# Books — direct CrossPoint Reader port to Shanling Q2

**Books** is an ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod)
on the Shanling Q2: a port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)'s
EPUB engine, layout and fonts to the Q2's Linux. It appears in Q2 Pod as **Books**
(card folder `.books`, program `.books/q2-books`). Earlier releases were named
differently; the first start of this version copies your reading positions,
bookmarks, highlights and settings from the old card folder into `.books`.

**Status:** 1.1.0 is the current release (1.0.0 was the first under the name Books)
(earlier releases were v0.3–v0.9 as CrossPod). It passes the host test suites
(GCC, Clang, ASan), EPUB fuzzing and the Q2 MIPS/QEMU build, but it has **not
yet been validated on a physical Q2**: see M1 below.
User notes: [v0.10–v0.11](docs/v0.10-v0.11-reading-dictionaries-opds.md),
[release notes](docs/release-notes/v1.1.0.md).

## Milestones

| Milestone | Scope | Status |
|---|---|---|
| M0 — HTTPS transport (v0.9) | HTTPS OPDS and KOReader sync through Q2 Pod's libcurl, mandatory certificate and hostname checks, same-origin redirects, stall timeouts | Code done and released. Open: tests against a real HTTPS Kavita server from a Q2 |
| M1 — Physical-device validation | Measured behavior on a real Q2: launch, wheel sensitivity, suspend/resume, SD and USB edge cases, Wi-Fi failures, RAM/CPU/battery/thermal | **Not started**: needs the device |
| M2 — Reader essentials (v0.10) | Go to percent, auto page turn, focus reading, TXT books, page counter and percent, 9 fonts at 10–12 pt | Done except RD-05 (user-installed fonts) |
| M3 — Dictionary, fonts, languages (v0.11) | StarDict dictionaries, paged definitions | Partly done (DI-01 to DI-03). Open items below |
| M4 — Library and servers (v0.12) | OPDS paging and catalog search | Partly done. Open items below |
| M5 — EPUB3 fidelity and resilience (v0.13) | Golden reference corpus compared with upstream CrossPoint; tables, ruby, RTL, footnotes, embedded fonts | Not started (fuzzing and stress tests exist) |
| M6 — Optional polish (v0.14) | Only features with a Q2 use case | Not started |
| M7 — Stable readiness (v1.0) | Device-proven daily reading | Version 1.0.0 is tagged; the criteria below are not all met yet |

## What still needs doing

**On a physical Q2 (M1, the release gate for "stable")**
- Fill a hardware QA matrix: firmware revision, release ZIP SHA-256, SD card type, pass/fail with photos and sanitized logs.
- Launch from Library; wheel at 10/50/100/200 % sensitivity; fast turns, Back, Centre and touch; auto page turn with and without Smart Awake.
- Screen off/on and Q2 Pod suspend; resume at the exact paragraph; forced exit during layout and downloads.
- SD card full, read-only, missing, unplugged and replugged; USB storage mode while running.
- Slow or missing Wi-Fi; an HTTPS Kavita server: browse, download, push/pull progress, refuse an invalid certificate with a readable error.
- Idle and active RAM, CPU, SD writes, battery drain and heat over a 30-minute session.
- Upgrade from CrossPod: check the one-time copy of positions, bookmarks and settings from `.crosspod` into `.books`.

**Reader and languages (M2/M3)**
- RD-05: install and select your own `.cpfont` fonts from the SD card; evaluate TTF/OTF only with a memory benchmark.
- Translated menus (Vietnamese and English first) and a menu font with CJK glyphs; menus show `?` for CJK today.
- Dictionary: Unicode case folding (e.g. `Đ`/`đ`), choosing which dictionaries to use, look-up history (vocabulary log), defining a selected phrase.
- Export highlights and clippings as plain text.

**Library and servers (M4)**
- Several saved OPDS servers (add, edit, delete) with their own credentials.
- Network cover thumbnails, an offline catalog cache, retry on failure.
- Folder tree with breadcrumbs; faster browsing of large libraries.
- Sync conflict handling, and syncing highlights where the server can store them.

**EPUB fidelity (M5)**
- A reference set of difficult books (tables, nested CSS, ruby/CJK, Indic/Arabic/RTL, anchors, images, embedded fonts, broken EPUBs) rendered here and on upstream CrossPoint with the same settings, with documented allowed differences.
- No known blank-page or data-corruption defects.

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

### Q2 MIPS / hardware diagnostic

The `books-hwcheck` diagnostic was merged from [PR #7](https://github.com/lampsonlnguyen/q2-crosspoint/pull/7). Its MIPS cross-build, ELF check and QEMU command-line smoke test passed, but it has **not been tested on the actual Q2 device**. The diagnostic is **not an ebook reader**; physical-device behavior and runtime compatibility are still unverified. See [MIPS hardware test gates](docs/q2-mips-hardware-validation.md) once that PR merges.

### End-to-end reader integration status

Source-level checks are intentionally distinct from an installable Q2 reader. The original
GfxRenderer, SD fonts, OpenType shaping and bitmap decompression are staged under
[draft PR #15](https://github.com/lampsonlnguyen/q2-crosspoint/pull/15), with a
mandatory full-link smoke test. The original XHTML chapter parser was compiler-validated
and merged in [PR #17](https://github.com/lampsonlnguyen/q2-crosspoint/pull/17).
The full renderer linker check still needs resolving upstream font/cache/shaping
implementation dependencies. Do **not** install the development binaries as
`/mnt/mmc/.books/books`; a correct reader executable has not been produced.
Physical Q2 testing, font coverage, actual page-turn latency, RAM and battery
measurements remain unverified until the full reader runs on the device.
