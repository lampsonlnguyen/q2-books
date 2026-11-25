# CrossPod — a port of CrossPoint Reader for Shanling Q2

**CrossPod** a.k.a. as Q2 Books, is an ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod)
on the Shanling Q2: a port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)'s
EPUB engine, layout and fonts to the Q2's Linux. It appears in Q2 Pod as **Books**
(card folder `.books`, program `.books/q2-books`). Earlier releases were named
differently; the first start of this version copies your reading positions,
bookmarks, highlights and settings from the old card folder into `.books`.

**Status:** [v1.5.0](https://github.com/lampsonlnguyen/q2-books/releases/tag/v1.5.0) is the current release and adds alphabetical navigation. The maintainer checked Grid View and the v1.4.2 library updates on a physical Shanling Q2. The broader hardware QA checklist is below.

## Automatic releases

Source changes merged to `main` automatically reserve the next patch version (or honor a newer explicitly selected `crosspoint-core/VERSION`), preserve existing version-specific release notes or generate a commit changelog, and dispatch the validated release workflow. The release ZIP and SHA-256 are published only after GCC/Clang and Q2 MIPS/QEMU checks pass. Feature branches remain test-only. README-only changes do not publish releases.

The **Automatic Books release** workflow can also be run manually on main. If version/tag preparation succeeds but a later build or dispatch fails, retry **Build, test and publish Books for Q2** with that existing tag; do not recreate or force-push the tag. Automatic release preparation requires the GitHub Actions token to be permitted to commit the version file and create tags. A failed build can reserve a version without publishing it. Automated tests do not replace physical-device QA.

## Release roadmap (v1.5.0–v1.18.0)

Each minor release has one primary feature. Patch releases address fixes, compatibility, and performance regressions. See [GitHub Releases](https://github.com/lampsonlnguyen/q2-books/releases) for completed releases.

| Release | Primary feature | Status |
| --- | --- | --- |
| v1.6.0 | Book information panel | In development |
| v1.7.0 | Collections and favorites | In development |
| v1.8.0 | Reading status filters | Planned |
| v1.9.0 | Recently read carousel | In development |
| v1.10.0 | Series grouping | In development |
| v1.11.0 | Cover Flow customization | Planned |
| v1.12.0 | In-book search | In development |
| v1.13.0 | Embedded EPUB fonts | In development |
| v1.14.0 | Markdown .md support | Planned |
| v1.15.0 | HTML support | Planned |
| v1.16.0 | Language-aware hyphenation | In development |
| v1.17.0 | Reading statistics | Planned |
| v1.18.0 | Reading streaks and goals | Planned |
| v1.19.0 | Read DRM-free .mobi files | Low Priority |
| v1.20.0 | Read DRM-free .prc files | Low Priority |

### v1.5.0 — Alphabetical navigation

In Browse files, tap **A-Z** or hold Centre and release to open the picker. Turn the wheel to a group and press Centre, or tap a group directly. Back cancels. The picker jumps to the first matching item in the current sort order and respects the current folder or search. Groups follow the same filenames as Title A-Z sorting. Accented Latin initials share their base letter, digits group under **0-9**, and non-Latin titles group by script.

- [ ] 1.5-M1 — Define alphabetical grouping and Unicode-aware collation consistent with library sorting; handle accents, numbers, symbols and non-Latin scripts.
- [ ] 1.5-M2 — Add Q2-style quick-jump selector and wheel/touch controls; skip empty groups.
- [ ] 1.5-M3 — Integrate with Cover Flow, Grid and List, honoring active search and filters; return focus to the selected book.
- [ ] 1.5-M4 — Test multilingual and 4000-book libraries, rapid wheel input and boundary behavior; keep index generation off the frame-critical path.

**Acceptance:** Users can jump to populated groups quickly without corrupting search, sort or selection.

### Testing the library and reader changes

- In **Browse files**, highlight a book and tap **Library** or hold Centre for just over a second and release. **Book details** shows local metadata and progress without opening the book. Turn the wheel to scroll the description and press Centre to open or resume. The shorter Centre hold still opens A–Z.
- Use **Library** to create and browse collections, add the selected book to collections, or toggle its favorite. **All books** clears the active group. Moved books retain membership when the card contains one matching file. The app leaves ambiguous or missing files associated with their saved paths.
- On **Home**, tap the arrows to cycle through cached recent books. With **Continue reading** highlighted, hold Centre and release to focus the carousel, then turn the wheel and press Centre to resume.
- **Library → Series** lists EPUB3 and Calibre series as their metadata is indexed. Select a series to browse in ascending volume order; books without a volume follow numbered books. Books outside a series are available under **All books**.
- Use **Reading settings → Find in book** or **Find** from the selection menu. The T9 keyboard enters ASCII queries; use selected text to search for accented or non-Latin words. From a result, turn the wheel for adjacent hits, press Centre for results, or press Back to return to the original page. Tap **Read from here** or hold Centre to resume reading at the hit. Results use bounded batches; chapters outside the parser's limits can be skipped.
- **Book fonts** uses a supported TTF or OTF family declared by a local stylesheet for the document body. Disable **Book fonts** to use your chosen reading typeface. Per-element families, WOFF, obfuscated fonts and fonts with restricted embedding fall back to the normal reading typeface or script font.
- **Hyphenation** uses book-language rules and supported XHTML `lang` / `xml:lang` overrides when enabled. Languages without a bundled pattern retain ordinary wrapping and explicit break opportunities.

### v1.6.0 — Book information panel

- [ ] 1.6-M1 — Extend bounded metadata extraction/cache for description, author, title, publisher, language and available identifiers.
- [ ] 1.6-M2 — Build themed detail panel with cover, metadata, reading progress and open/resume action; wrap multilingual text and scroll long descriptions.
- [ ] 1.6-M3 — Add discoverable wheel/button and touch access from all library views without interfering with opening books; distinguish local details from online catalog actions.
- [ ] 1.6-M4 — Handle absent/malformed metadata, safe plain-text description rendering, cache invalidation and localization; regression-test EPUB2/EPUB3.

**Acceptance:** View book details without opening or paginating the book; missing metadata does not block reading.

### v1.7.0 — Collections and favorites

- [ ] 1.7-M1 — Design versioned, bounded, atomic SD-backed storage for collection names, membership and favorites; use stable book identity.
- [ ] 1.7-M2 — Implement create, rename and delete collection, add/remove membership and favorite toggle using accessible wheel/touch menus.
- [ ] 1.7-M3 — Integrate collection and favorite browsing with all views, sort and search; permit books in multiple collections.
- [ ] 1.7-M4 — Handle renamed, removed and re-added files, duplicate names, empty collections and interrupted/read-only writes.

**Acceptance:** Collections and favorites survive restarts; deleting a collection never deletes ebook files or reading state.

### v1.8.0 — Reading status filters

- [ ] 1.8-M1 — Define unread, reading and completed states using existing reading history and whole-book progress; handle legacy positions without a percentage.
- [ ] 1.8-M2 — Provide All, Unread, Reading and Completed filters and counts; allow explicit mark read/unread without discarding resume position.
- [ ] 1.8-M3 — Combine filters with collections, search, sort and library views; persist selected filter and maintain valid selection.
- [ ] 1.8-M4 — Test completion boundaries, old state migration, rereading and missing files.

**Acceptance:** Status is consistent across views and restarts and does not falsely mark unknown-progress books completed.

### v1.9.0 — Recently read carousel

- [ ] 1.9-M1 — Reuse existing recent-book history and color thumbnail cache; define bounded recency order and missing-file handling.
- [ ] 1.9-M2 — Add compact home-screen carousel with current book, title, author and progress, keeping Browse and other home actions accessible.
- [ ] 1.9-M3 — Support wheel/touch selection and one-action resume at the saved location; refresh recency only after successful opens.
- [ ] 1.9-M4 — Validate no-history, single-book and full-history layouts across themes and languages; test startup latency.

**Acceptance:** Resume a recent book directly from Home without opening the full library or rebuilding artwork.

### v1.10.0 — Series grouping

- [ ] 1.10-M1 — Parse EPUB series metadata, including supported Calibre and EPUB3 collection conventions and numeric volume order.
- [ ] 1.10-M2 — Cache normalized series identity, display name and volume; handle missing, fractional and duplicate volume numbers.
- [ ] 1.10-M3 — Add series browser and volume ordering, compatible with collection/status filters, search and existing views.
- [ ] 1.10-M4 — Test multilingual names, standalone books, mixed metadata conventions, sorting and cache invalidation.

**Acceptance:** Series group reliably, volumes appear in meaningful numeric order and standalone titles remain accessible.

### v1.11.0 — Cover Flow customization

- [ ] 1.11-M1 — Define bounded settings for cover size, spacing, reflections and animation speed; use firmware-style defaults.
- [ ] 1.11-M2 — Add themed wheel/touch settings controls and immediate visual preview; support reset to defaults.
- [ ] 1.11-M3 — Persist settings atomically and clamp invalid values; adapt geometry, hit testing and cache reach consistently.
- [ ] 1.11-M4 — Test extreme settings, rounded-screen clipping, light/dark themes, rendering cost and idle behavior.

**Acceptance:** Customization stays within safe geometry and memory limits and does not alter reader typography or system theme.

### v1.12.0 — In-book search

- [ ] 1.12-M1 — Design chapter-streaming text search using the existing EPUB/TXT engine, normalized Unicode and bounded buffers.
- [ ] 1.12-M2 — Add query entry, next/previous match, snippets and chapter-aware results; make long searches cancellable.
- [ ] 1.12-M3 — Jump to the matching text using stable chapter/text anchors, highlight matches, and provide return to the original reading position.
- [ ] 1.12-M4 — Test Vietnamese accents, CJK, RTL, chapter boundaries, very large books, corrupt content and different font sizes.

**Acceptance:** Search across the entire book without loading the entire text into RAM; match navigation preserves a recoverable reading position.

### v1.13.0 — Embedded EPUB fonts

- [ ] 1.13-M1 — Inspect CrossPoint CSS/font pipeline and EPUB manifest handling; resolve supported @font-face references safely inside archives.
- [ ] 1.13-M2 — Validate supported TTF/OTF font data and license/embedding restrictions; reject unsupported, oversized, malformed or unsafe resources with fallback.
- [ ] 1.13-M3 — Implement bounded on-demand font loading, glyph caching and family/style selection; respect publisher CSS and user override.
- [ ] 1.13-M4 — Preserve script fallback, shaping, pagination and text anchors; test mixed-script books, missing glyphs and memory under font churn.

**Acceptance:** Supported embedded fonts render faithfully with safe fallback and no regressions in reading resume or resource usage.

### v1.14.0 — Markdown support

- [ ] 1.14-M1 — Recognize .md files in the local library and define safe conversion to the existing XHTML/layout pipeline (no separate reader).
- [ ] 1.14-M2 — Support headings, paragraphs, lists, emphasis, links, code blocks, blockquotes and supported local images; document unsupported extensions.
- [ ] 1.14-M3 — Integrate chapter navigation, reading position, bookmarks, search, typography and library metadata.
- [ ] 1.14-M4 — Bound conversion resources, handle UTF-8 and malformed input, restrict local paths and disable active/remote content by default; test long and multilingual files.

**Acceptance:** Open Markdown directly from Books, with reliable rendering and resume, using the existing CrossPoint engine.

### v1.15.0 — HTML support

- [ ] 1.15-M1 — Recognize .html and .htm files; adapt safe HTML parsing/repair to the existing reader pipeline.
- [ ] 1.15-M2 — Support common static HTML text, headings, lists, links, supported CSS and local images; no browser engine or JavaScript execution.
- [ ] 1.15-M3 — Integrate anchors, navigation, reading state, bookmarks, search and library metadata; keep local resource access confined to allowed paths.
- [ ] 1.15-M4 — Test malformed HTML, encodings, multilingual pages, missing resources, remote URLs and large documents; document fidelity limits.

**Acceptance:** Read local static HTML offline without scripts, unrestricted file access or automatic remote requests.

### v1.16.0 — Language-aware hyphenation

- [ ] 1.16-M1 — Read EPUB language metadata and supported inline language overrides; normalize language tags independently of system UI language.
- [ ] 1.16-M2 — Reuse existing Liang hyphenation helpers and licensed dictionaries/patterns; load only needed languages with bounded caches.
- [ ] 1.16-M3 — Add automatic language selection, preserve user hyphenation controls and fall back safely when no rules are available.
- [ ] 1.16-M4 — Test multilingual books, mixed-language passages, CJK/RTL line-breaking, layout changes and saved text-anchor restoration.

**Acceptance:** Supported languages use correct rules without overriding intentional user settings or breaking other scripts.

### v1.17.0 — Reading statistics

- [ ] 1.17-M1 — Define active reading time, pages read, completed books and daily averages; exclude idle, menus, suspend and background activity.
- [ ] 1.17-M2 — Design compact versioned local statistics storage with rate-limited atomic saves and crash recovery; do not transmit reading activity.
- [ ] 1.17-M3 — Add themed statistics screen with daily/weekly totals and per-book summaries; avoid double-counting backward turns, repagination and repeated resume events.
- [ ] 1.17-M4 — Test session boundaries, clock changes, offline operation, rereads, unexpected exits and long-session write/CPU cost.

**Acceptance:** Statistics survive restart and reflect defined active-reading behavior without affecting progress or battery materially.

### v1.18.0 — Reading streaks and goals

- [ ] 1.18-M1 — Build on v1.17.0 statistics; define qualifying reading days, current/longest streak, daily minute/page targets and local date boundaries.
- [ ] 1.18-M2 — Add configurable daily goals, progress indicators and streak summary using native theme and wheel/touch controls.
- [ ] 1.18-M3 — Persist goal preferences; handle missed days, timezone/clock changes, month/year boundaries and goal changes predictably.
- [ ] 1.18-M4 — Test deterministic streak calculations, offline usage, restart, missing history and localization; keep reminders optional or omit them if firmware does not support them safely.

**Acceptance:** Goals and streaks derive from accurate local statistics, never invent reading days, and require no background service.

### Shared engineering and release gates

- Preserve faithful CrossPoint EPUB rendering, multilingual support, responsive click-wheel navigation and low RAM/CPU use, in that priority order. The Q2 has 128 MB physical RAM shared with Linux, firmware and audio services; working-memory targets are not measured results.
- Keep the existing source-level CrossPoint reader, pagination, Unicode/shaping, fonts and storage architecture. Reuse bounded caches, streaming parsing and incremental processing; avoid new dependencies unless justified.
- Q2 Pod handles device power, suspend and shutdown. Books saves its state and exits cleanly; do not port ESP32 firmware or introduce an independent power-management service.
- Follow Q2 Pod system theme, language packs and wheel sensitivity. Verify touch, Back and Centre, rounded-screen bounds, light/dark variants and multilingual text. Add translated UI labels for every release, with safe English fallback.
- Maintain compatibility with positions, bookmarks, highlights, settings, library cache and network/OPDS functionality. Version new persistent formats, validate input, recover from corrupt or read-only storage, and minimize SD writes.
- Use [Q2 Pod](https://github.com/DiamondBond/q2-pod) as the primary visual/interaction reference; [CrossInk](https://github.com/uxjulia/CrossInk) for rectangular book-cover layout; [Q2-Boy](https://github.com/lampsonlnguyen/q2-boy) for texture caching and prefetch; [Q2-PICO-8](https://github.com/lampsonlnguyen/q2-pico8) for perspective rendering and touch; [CrossPoint](https://github.com/crosspoint-reader/crosspoint-reader) and [KOReader](https://github.com/koreader/koreader) for reader behavior and architecture. Preserve attribution and comply with original licenses; public source availability alone does not waive license obligations.
- For each release: work on a feature branch, add focused regression tests, pass GCC/Clang host tests and Q2 MIPS cross-build, check simulator visuals and device behavior, measure memory/CPU/input latency and relevant battery/SD impact, document changes and known limits, review compatibility and licenses, merge only after validation, then publish versioned card ZIP, SHA-256 and release notes. Physical-Q2 validation remains the stable-release gate (M1 below).
- Keep historical M0–M7 porting milestones and their outstanding work in scope as cross-cutting requirements; do not treat this roadmap as evidence those tasks are complete.

### Explicitly out of scope

DRM-free MOBI/PRC support, CBZ comic book support, custom reading themes and text-to-speech are excluded from this roadmap. Cover Flow customization is limited to library presentation and is not a custom reading-theme feature.

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
