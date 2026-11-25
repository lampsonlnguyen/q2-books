# CrossPod — Books for Shanling Q2

CrossPod (Q2 Books) is a standalone ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod), porting the actual [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) engine to the Q2's MIPS Linux environment. No ESP32 firmware is shipped. The project prioritizes faithful rendering, multilingual text, responsive wheel controls and low memory/CPU use on the Q2's 128 MB of shared RAM.

## Install

1. Download an installable `books-q2-v*.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-books/releases) and verify its accompanying SHA-256 checksum.
2. Extract to the microSD root, preserving the hidden `.books` folder and existing reading data. Place EPUB, TXT, Markdown (`.md`), or local HTML (`.html` / `.htm`) files under `Books/`.
3. Open **Library → Books** in Q2 Pod (older firmware may list card apps under Games). No firmware flashing is required.

## Features in the source

- EPUB, TXT, Markdown, and local HTML reader using CrossPoint pagination, CSS, illustrations, multilingual shaping and font fallback.
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

## Upcoming milestones

The following items are outstanding in the [detailed roadmap](docs/ROADMAP.md). Version numbers are planning targets, not guaranteed release dates. Implemented features that still need hardware testing are listed separately from new features.

### v1.11.0 — Cover Flow customization

- [ ] M1 — Bounded cover size, spacing, reflection and animation speed settings, with firmware-style defaults.
- [ ] M2 — Wheel/touch settings, immediate preview and reset to defaults.
- [ ] M3 — Atomic settings persistence, safe value clamping, consistent layout, hit testing and cache reach.
- [ ] M4 — Test extreme settings, rounded-screen clipping, light/dark themes, rendering cost and idle behavior.

### Remaining feature milestones

- [ ] M3 — Reader and languages: native-speaker translation review; additional Japanese, Korean, Thai and Chinese labels; benchmark direct reading-font TTF/OTF loading before expanding support.
- [ ] M4 — Library and servers: Kavita highlight synchronization through its separate REST API; benchmark 4,000-book indexing and folder browsing.
- [ ] M5 — EPUB fidelity: compare rendering against upstream CrossPoint with identical settings; document ruby/table differences; extend the corpus to full-length books with tables, ruby, CJK and Indic text; measure layout RAM and page-turn time.
- [ ] M6 — Polish: benchmark smooth text and add a reading-settings switch; investigate firmware 12/24-hour clock integration; refresh the clock in open menus and show time remaining in the chapter.
- [ ] M7 — Release and packaging: device compatibility report, troubleshooting guide, user manual, font/dependency license audit, upstream App Store and Books-entry integration, and eventual removal of the legacy migration after upgrades are complete.

### Remaining testing and stable-release gates

- [ ] Physical Q2 validation of Markdown and local HTML: links, navigation, images, pagination, resume and peak RAM.
- [ ] Reading activity: SD writes, idle CPU, battery, screen-off/suspend exclusion, and wheel/touch menus in both themes.
- [ ] In-book search: EPUB/TXT, multilingual queries, chapter boundaries, corrupt chapters, bounded batches, saved anchors, latency and peak RAM; execute pending host regressions.
- [ ] Library regression coverage: alphabetical navigation on multilingual 4,000-book shelves, metadata/cache/localization, collections, reading status, recent carousel (including empty/single/full history and the new Centre/Return focus interaction), and series sorting.
- [ ] Font and layout regression coverage: embedded fonts, fallback, memory churn, hyphenation, CJK/RTL, font changes and source-anchor resume.
- [ ] Stable-release hardware matrix: firmware version, package checksum, SD type, launch, wheel sensitivity, touch, Smart Awake, screen off/on, suspend, exact-position resume, forced exit, storage faults, USB mode, Wi-Fi/HTTPS, invalid certificates, resource usage and legacy data migration.

### Outside the active roadmap

DRM-free MOBI/PRC, CBZ, custom reader themes and text-to-speech remain deferred until their resource cost and compatibility can be evaluated. Clickwheel sound integration is not planned.

## Development

- [Technical reference](docs/TECHNICAL.md) — architecture, features, storage, controls, limits and design constraints.
- [Outstanding roadmap](docs/ROADMAP.md) — unfinished features and validation only.
- [Build and release guide](docs/releasing.md) — local tests, CI, packaging and troubleshooting.
- [GitHub Actions](https://github.com/lampsonlnguyen/q2-books/actions) — current build results.

Q2 Pod owns power and suspend; Books checkpoints reading state and exits cleanly. Preserve upstream CrossPoint algorithms, bounded caches and streaming I/O. Do not add heavyweight dependencies or independent power-management services.

## Licensing

See [CrossPoint's original MIT license](crosspoint-core/LICENSE-CROSSPOINT) and the third-party notices under `crosspoint-core/licenses/`. Preserve upstream and dependency attribution.
