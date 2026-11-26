# CrossPod — Books for Shanling Q2

CrossPod (Q2 Books) is a standalone ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod), porting the actual [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) engine to the Q2's MIPS Linux environment. No ESP32 firmware is shipped. The project prioritizes faithful rendering, multilingual text, responsive wheel controls and low memory/CPU use on the Q2's 128 MB of shared RAM.

## Install

1. Download an installable `books-q2-v*.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-books/releases) and verify its accompanying SHA-256 checksum.
2. Extract to the microSD root, preserving the hidden `.books` folder and existing reading data. Place EPUB, TXT, Markdown (`.md`), local HTML (`.html` / `.htm`), or supported DRM-free MOBI/PRC files under `Books/`.
3. Open **Library → Books** in Q2 Pod (older firmware may list card apps under Games). No firmware flashing is required.

## Features in the source

- EPUB, TXT, Markdown, local HTML and supported DRM-free MOBI/PRC use CrossPoint pagination, illustrations, multilingual shaping and font fallback. Legacy MOBI/PRC has a dedicated bounded importer with basic formatting, page breaks, internal links and metadata.
- Cover Flow, Grid and List library views; alphabetical jump, search, metadata, favorites, collections, reading status and numeric series grouping.
- Home recent-books carousel with color covers, progress and resume; wheel and touchscreen navigation.
- Bookmarks, highlights, dictionary lookup, footnote/link navigation, reading progress and typography controls; installed reading font sizes 8–14 pt.
- Local reading statistics, per-book totals, daily minute and page goals, and reading streaks.
- EPUB in-book search with bounded results, highlighted matches and return to the original position.
- Supported publisher TTF/OTF fonts with fallback and user override; language-aware hyphenation using bundled patterns.
- OPDS catalog browsing and downloads over HTTPS, KOReader position synchronization, optional SD-backed font packs and system theme/language integration.

Source features may be ahead of the last successfully published binary. CI and physical-device testing are distinct; not all features have completed hardware validation.

## Controls

Wheel moves through menus and pages; Centre opens/confirms; Back returns or exits. On Home, turn the wheel up from Continue Reading to highlight the recent carousel, press Centre to activate it, turn the wheel to browse books, and press Centre to resume or Return to restore vertical navigation. Browse files supports A–Z jump and Library tools; a longer Centre hold opens Book Details. Reading settings provide typography, search and other reader options. While reading in light mode, select **Page warmth** to cycle through Cool, Default, Warm and Warmer. Books saves the choice for your next reading session.

## Development roadmap — 2.x

The 1.x series establishes the core Books experience. Starting with **v2.0.0**, new releases focus on optional enhancements, additional formats, refinement and compatibility rather than rebuilding the reader. Preserve the existing reading engine, user data, UI, responsive controls and low memory/CPU usage on the Q2's 128 MB of shared RAM.

The following version assignments are planning targets, not release commitments. All unchecked items are outstanding. Hardware validation remains distinct from feature completion; a mature core does not imply that every physical-device test has passed.

### v2.0.0 — Cover Flow customization

- [ ] 2.0-M1 — Define bounded settings for cover size, spacing, reflections and animation speed, using firmware-style defaults.
- [ ] 2.0-M2 — Add themed wheel/touch settings controls, immediate visual preview and reset to defaults.
- [ ] 2.0-M3 — Persist settings atomically; clamp invalid values and adapt geometry, hit testing and cache reach consistently.
- [ ] 2.0-M4 — Test extreme settings, rounded-screen clipping, light/dark themes, rendering cost and idle behavior.

Acceptance: customization stays within safe geometry and memory limits without altering reader typography or system theme.

### v2.0.6 target — DRM-free MOBI and PRC support

Rewrite status: bounded standalone decoder/importer, metadata and artwork, and reader integration are implemented on `feature/mobi-prc-v2`. Actual-file host import tests passed; full host/Q2 integration and physical-device performance remain to be validated before merge. The earlier v2.0.4 implementation is archived.

- [ ] 2.1-M1 — Evaluate a lightweight, license-compatible PalmDB/PalmDOC/MOBI parser and establish supported MOBI/PRC variants, compression and resource limits.
- [ ] 2.1-M2 — Add file discovery, metadata, author/title and cover extraction for supported DRM-free .mobi and .prc ebooks; distinguish ebooks from unrelated PRC files.
- [ ] 2.1-M3 — Stream or incrementally decode supported text and embedded resources into the existing CrossPoint-compatible reading pipeline; reuse pagination, fonts and multilingual rendering rather than creating a separate reader.
- [ ] 2.1-M4 — Integrate chapter navigation, resume, reading progress, bookmarks, highlights and search where supported; document unsupported formatting and features.
- [ ] 2.1-M5 — Handle encrypted/DRM-protected, malformed, truncated and unsupported files gracefully, with bounded decompression and readable errors. No DRM removal is planned.
- [ ] 2.1-M6 — Add host and MIPS/QEMU tests with representative public-domain or synthetic DRM-free files; verify rendering, navigation, resume, latency, peak RAM and battery on physical Q2 hardware.

Acceptance: supported DRM-free MOBI/PRC books open reliably with the existing reading controls, without regressing EPUB/TXT/Markdown/HTML or exceeding practical Q2 resource limits. Exact legacy MOBI, KF8/hybrid and PalmDOC compatibility is subject to parser evaluation and testing.

### v2.2.0 — Reader and language enhancements

- [ ] Native-speaker review of the [menu translations](crosspoint-core/lang/README.md).
- [ ] Add Books-specific labels for Japanese, Korean, Thai and Chinese.
- [ ] Benchmark direct reading TTF/OTF fonts on Q2 before expanding support beyond the current .cpfont files in Fonts/. Keep embedded-font support separate from external reading-font loading.

### v2.3.0 — Library and server enhancements

- [ ] Add Kavita highlight synchronization through its separate REST API, including authentication, chapter identifiers and highlight format compatibility (Kavita 0.8.8+). KOReader synchronization currently stores reading position only.
- [ ] Measure and optimize 4,000-book scanning and folder browsing on a physical Q2 SD card.

### v2.4.0 — EPUB fidelity and rendering refinement

- [ ] Compare the test corpus pixel for pixel with upstream CrossPoint using identical settings; document allowed differences, including ruby and table layout.
- [ ] Extend the corpus from public-domain excerpts to full-length books with tables, ruby, CJK and Indic text.
- [ ] Measure chapter-layout RAM and page-turn latency on Q2, including the existing parse changes.

### v2.5.0 — Reading and UI polish

- [ ] Measure page-turn time and battery impact of smooth text (currently draws each new page five times); add a Reading settings switch.
- [ ] Investigate firmware 12/24-hour clock integration. The current clock is 24-hour only; the system preference is not currently readable by the card app.
- [ ] Refresh the menu bar clock while a menu remains open.
- [ ] Add time remaining in the current chapter.

### v2.6.0 — Distribution, documentation and packaging

- [ ] Publish a device compatibility and test report.
- [ ] Expand the troubleshooting guide and add a user manual.
- [ ] Complete the font and dependency license audit; bundled font licenses are collected in .books/fonts/CrossPoint-font-licenses.txt.
- [ ] Coordinate Q2 Pod App Store integration: upstream must remove the reserved books ID (patch/netupdate.c) and enable lampsonlnguyen/q2-books in sources.list. Until then, install from the release ZIP at the SD card root.
- [ ] Coordinate removal of the duplicate built-in Books Library entry when upstream hands over the name.
- [ ] Remove the one-time .crosspod migration only after legacy upgrades are complete.

### Continuous validation — existing features and physical-device QA

These checks apply throughout 2.x and do not imply that the listed features still need to be implemented.

**Document formats and reading activity**

- [ ] Markdown: verify chapter links, images, pagination, resume and peak RAM on a physical Q2.
- [ ] Local HTML: verify chapter navigation, local images, resume and peak RAM.
- [ ] Reading activity: measure SD writes, idle CPU and battery during long reading sessions.
- [ ] Verify screen-off and suspend exclusion against firmware power state; test both activity menus using wheel and touch in light/dark themes.
- [ ] In-book search: validate EPUB/TXT multilingual queries, chapter boundaries, skipped corrupt chapters, bounded batches, saved anchors, latency and peak RAM; run the pending host TXT/Unicode/multi-batch regressions.

**Library, font and layout regressions**

- [ ] Alphabetical navigation: multilingual 4,000-book shelves and rapid wheel input.
- [ ] Book details: missing/malformed metadata, cache invalidation and localization.
- [ ] Collections: moves, duplicates, empty groups and interrupted/read-only writes.
- [ ] Reading status: completion boundaries, legacy state, rereading and missing files.
- [ ] Recent carousel: empty/single/full history, themes, languages, startup latency, Centre activation and Return to vertical navigation.
- [ ] Series: multilingual names, standalone titles, mixed metadata, numeric sorting and cache invalidation.
- [ ] Embedded fonts: mixed scripts, fallback, pagination and memory churn.
- [ ] Hyphenation: nested language scopes, mixed scripts, CJK/RTL, font changes and source-anchor resume.

**Physical Q2 stable-release qualification**

- [ ] Record hardware QA matrix: firmware revision, release ZIP SHA-256, SD card type, pass/fail, photos and sanitized logs.
- [ ] Launch from Library; test wheel sensitivity at 10/50/100/200%, rapid turns, Back, Centre and touch; auto page turn with and without Smart Awake.
- [ ] Test screen off/on and firmware suspend; exact-paragraph resume; forced exit during layout and downloads.
- [ ] Test full, read-only, missing, removed and reinserted SD cards; USB storage mode while running.
- [ ] Test slow/missing Wi-Fi and HTTPS Kavita browsing, downloads and progress push/pull; reject invalid certificates with a readable error.
- [ ] Measure idle/active RAM, CPU, SD writes, battery drain and heat over a 30-minute session.
- [ ] Verify one-time migration of reading positions, bookmarks and settings from .crosspod to .books on upgrade.

### Deferred / not planned

CBZ, custom reader themes and text-to-speech remain deferred until their resource cost and compatibility can be evaluated. DRM-free MOBI/PRC is now on the active roadmap; DRM-protected ebooks and clickwheel sound integration are not planned.

## Development

- [Technical reference](docs/TECHNICAL.md) — architecture, features, storage, controls, limits and design constraints.
- [Outstanding roadmap](docs/ROADMAP.md) — unfinished features and validation only.
- [Build and release guide](docs/releasing.md) — local tests, CI, packaging and troubleshooting.
- [GitHub Actions](https://github.com/lampsonlnguyen/q2-books/actions) — current build results.

Q2 Pod owns power and suspend; Books checkpoints reading state and exits cleanly. Preserve upstream CrossPoint algorithms, bounded caches and streaming I/O. Do not add heavyweight dependencies or independent power-management services.

## Licensing

See [CrossPoint's original MIT license](crosspoint-core/LICENSE-CROSSPOINT) and the third-party notices under `crosspoint-core/licenses/`. Preserve upstream and dependency attribution.
