# CrossPod — Books for Shanling Q2

CrossPod (Q2 Books) is a standalone ebook reader card app for [Q2 Pod](https://github.com/DiamondBond/q2-pod), porting the actual [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) engine to the Q2's MIPS Linux environment. No ESP32 firmware is shipped. The project prioritizes faithful rendering, multilingual text, responsive wheel controls and low memory/CPU use on the Q2's 128 MB of shared RAM.

## Install

1. Download an installable `books-q2-v*.zip` from [GitHub Releases](https://github.com/lampsonlnguyen/q2-books/releases) and verify its accompanying SHA-256 checksum.
2. Extract to the microSD root, preserving the hidden `.books` folder and existing reading data. Place EPUB, TXT, or Markdown (.md) files under `Books/`.
3. Open **Library → Books** in Q2 Pod (older firmware may list card apps under Games). No firmware flashing is required.

## Features in the source

- EPUB, TXT, and Markdown reader using CrossPoint pagination, illustrations, multilingual shaping and font fallback.
- Cover Flow, Grid and List library views; alphabetical jump, search, metadata, favorites, collections, reading status and numeric series grouping.
- Home recent-books carousel with color covers, progress and resume; wheel and touchscreen navigation.
- Bookmarks, highlights, dictionary lookup, footnote/link navigation, reading progress and typography controls; installed reading font sizes 8–14 pt.
- EPUB in-book search with bounded results, highlighted matches and return to the original position.
- Supported publisher TTF/OTF fonts with fallback and user override; language-aware hyphenation using bundled patterns.
- OPDS catalog browsing and downloads over HTTPS, KOReader position synchronization, optional SD-backed font packs and system theme/language integration.

Source features may be ahead of the last successfully published binary. CI and physical-device testing are distinct; not all features have completed hardware validation.

## Controls

Wheel moves through menus and pages; Centre opens/confirms; Back returns or exits. On Home, hold Centre on Continue Reading to focus the recent carousel, then turn the wheel and press Centre to resume. Browse files supports A–Z jump and Library tools; a longer Centre hold opens Book Details. Reading settings provide typography, search and other reader options.

## Development

- [Technical reference](docs/TECHNICAL.md) — architecture, features, storage, controls, limits and design constraints.
- [Outstanding roadmap](docs/ROADMAP.md) — unfinished features and validation only.
- [Build and release guide](docs/releasing.md) — local tests, CI, packaging and troubleshooting.
- [GitHub Actions](https://github.com/lampsonlnguyen/q2-books/actions) — current build results.

Q2 Pod owns power and suspend; Books checkpoints reading state and exits cleanly. Preserve upstream CrossPoint algorithms, bounded caches and streaming I/O. Do not add heavyweight dependencies or independent power-management services.

## Licensing

See [CrossPoint's original MIT license](crosspoint-core/LICENSE-CROSSPOINT) and the third-party notices under `crosspoint-core/licenses/`. Preserve upstream and dependency attribution.
