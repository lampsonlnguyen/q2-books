# Books technical reference

## Architecture and design constraints

Books is a source-level CrossPoint EPUB/TXT/Markdown/HTML port, not Q2 Pod's stock flat-text EPUB reader. The independent Linux process uses the firmware only for launch, input/display handoff, system preferences and power lifecycle. Q2 has 128 MB physical RAM shared with Linux and audio; avoid treating this as the application's available heap. Prioritize EPUB fidelity, multilingual support, responsiveness, then bounded resource usage. No ESP32 firmware, Arduino runtime, browser engine or background power service is shipped.

| Layer | Source / responsibility |
| --- | --- |
| Application | `crosspoint-core/reader/books_reader.cpp`: Reader, CardShelf, event loop and menus |
| EPUB | `crosspoint-core/lib/Epub/`: OPF/container, spine, CSS, ChapterHtmlSlimParser, ParsedText, Section, Page and disk-backed metadata/page caches |
| Markdown | `reader/MarkdownPackage.cpp`: CommonMark conversion to a cached EPUB |
| HTML | `reader/HtmlPackage.cpp`: bounded static HTML conversion to a cached EPUB |
| TXT | `lib/Txt/` and `reader/TxtPackage.hpp`: conversion through existing layout pipeline |
| Archive | `lib/ZipFile/`: named resources and streamed decompression |
| Typography | EpdFont/EpdFontFamily, GfxRenderer, ScriptFonts, Typography, EmbeddedBookFont |
| Library | ShelfLibrary, ShelfIndexer, ShelfBookInfo, ShelfCollections, ShelfReadingStatus, ShelfHistory, ShelfSeries, ShelfSearch, ShelfSort |
| Connected | ConnectedHttp, ConnectedConfig, OpdsCatalog, KoreaderSync, FontPacks |
| Q2 | `platform/q2/`: Linux adapters, input translation and display presentation |

Paths in this table are relative to `crosspoint-core/` unless fully specified. The original CrossPoint source and licenses are retained; Q2-specific adapters and reader shell replace ESP32 hardware dependencies.

## Rendering and languages

EPUB container and OPF metadata identify the ordered spine. XHTML/CSS chapter parsing feeds CrossPoint text layout, font metrics, bidi/shaping and page cache; images and page artwork use bounded decoding. The logical landscape UI is 375 × 320; the physical framebuffer is 320 × 375. Native rotation maps logical (x,y) to physical (319-y,x). The reader paints during its ownership interval and yields to firmware on exit.

The default extended Noto Sans supports Latin and Vietnamese, with optional script/font packs on SD. Installed families offer 8–14 pt where available. Unicode NFC, CJK line breaking, Arabic/Hebrew bidi and script shaping are supported by the existing engine; coverage and shaping fidelity depend on installed fonts and the underlying implementation. Publisher TTF/OTF support is limited to a supported body-family CSS declaration, 1 MiB per font and 2 MiB aggregate source budget, four styles and 24 cached glyphs per style. WOFF, obfuscated/restricted fonts and unsupported declarations fall back. The separate v1.13 optimization is not merged.

Hyphenation uses book language and inline lang/xml:lang independently of UI locale. Eleven licensed static Liang patterns cover en/fr/de/es/it/pt/sv/fi/pl/ru/uk; no heap-loaded dictionary corpus. User Off and heading suppression disable automatic patterns; unsupported languages retain ordinary wrapping and explicit break opportunities. Layout cache versions protect changed pagination.

## Library and reader behavior

The SD shelf supports Cover Flow/Grid/List, folder and title sorting, alphabetical jumps, search, metadata details, collections, favorites and reading status. Membership storage is separate from ebook files. Reading status combines history/progress and manual overrides without deleting positions. The recent carousel reuses bounded history (20 entries) and cached color covers; selection does not update history until successful open.

Series uses Calibre and EPUB3 collection metadata; normalized names group volumes, numeric order includes fractions and unnumbered books follow numbered books. Existing versioned metadata cache is invalidated on source identity changes. Compact series references occupy eight bytes each excluding container/name overhead. Indexing is incremental on idle ticks; series scans skip already indexed entries and do not decode covers. Normal library operations should not load book chapters solely to browse metadata.

Reading includes resume, bookmarks, highlights, clippings, dictionary lookup, links, footnotes, page/percentage/chapter navigation and typography. EPUB in-book search streams chapter XML in 4 KiB chunks and stores up to 256 matches per batch. Results can jump to text offsets, highlight and restore the original position. TXT, Markdown, and HTML search use the same cached EPUB package and source-coordinate mapping. T9 entry is ASCII; selected text permits non-Latin queries. Search previews do not persist reading progress until explicitly accepted. Oversized or corrupt chapters can be skipped.

## Markdown

Place UTF-8 `.md` files under `Books/`. Books uses the filename as the library title and keeps
reading state with the original file. Markdown renders through the normal CrossPoint reader.
Headings populate the table of contents. Paragraphs, lists, emphasis, inline and fenced code,
blockquotes and same-document heading links are supported. Other link destinations retain their
label as text. Relative PNG and JPEG images can be read beside or below the document, with
32 images, 512 KiB per image and 2 MiB total. Missing or rejected images display their alt text.

Conversion accepts up to 1 MiB of source and replaces malformed UTF-8. Large documents are
split at block boundaries. An individual block that exceeds the 256 KiB generated chapter limit prevents the book from opening. Raw HTML is escaped as text. Remote resources,
paths outside the document directory, and symbolic links are excluded. GFM tables, task lists,
footnote extensions and front matter metadata are outside the current format support.

The parser is [MD4C 0.5.3](https://github.com/mity/md4c/tree/release-0.5.3),
vendored under `lib/md4c/`. Its MIT notice is in `licenses/MD4C.md` and in the release ZIP.

## Local HTML

Place `.html` or `.htm` files under `Books/`. Common static text, headings, lists,
tables, code, links to anchors in the same document, and local PNG/JPEG artwork
render through the CrossPoint reader. The source file owns reading state.
The converter repairs common omitted closing tags, reads UTF-8 and BOM-marked
UTF-16, and interprets malformed UTF-8 bytes as Windows-1252. An HTML title
supplies the book title with the filename as fallback.

Inline styles and embedded stylesheets support the reader's basic text and
layout properties. Scripts, forms' interactive controls, frames, external links
and stylesheets, and remote media are excluded. Relative images must be beside
or below the document with no symlinks or parent traversal. Missing images
show their alt text. The converter accepts up to 1 MiB of source, 256 KiB
of markup per balanced chapter, and 32 images of up to 512 KiB each and 2 MiB
total. Complex layouts and deeply nested or very large single sections may
exceed these limits.

## Storage, lifecycle and networking

Install at SD root: `.books/q2app`, `.books/q2-books` launcher, `.books/books-engine` MIPS executable, `.books/fonts/` and `Books/` content. Reading settings, bookmarks, highlights, history, caches and other state live under `.books`. Legacy `.crosspod` state is migrated on first start. Avoid deleting `.books` when updating. Versioned state formats and source identity checks protect against stale data; changed reading position is periodically checkpointed, flushed on clean exit/signals, and sudden power loss can lose the last unflushed change. Q2 Pod controls shutdown, sleep and wake.

Q2 input arrives as Unix datagrams (wheel, key down/up and touch down/move/up), translated to logical actions. Respect firmware wheel sensitivity, theme and UI language with safe fallbacks. Firmware integration may require newer Library row support; do not replace firmware or system libraries.

OPDS catalogs and book downloads use HTTPS with certificate verification and a bundled CA store. KOReader synchronization is position-only; Kavita highlight REST synchronization is not implemented. Optional font downloads are streamed and checksum verified. Keep networking user-driven and avoid background services.

## Memory and performance policy

- Reuse CrossPoint streaming ZIP/XML and disk-backed metadata/page caches; bound decoded images, text buffers, search results and glyph caches.
- Keep only selected/needed fonts and bounded artwork caches resident; use idle indexing/prefetch and repaint on changes, not continuously.
- Avoid full-library reparsing, persistent duplicate metadata, unbounded collections and unnecessary SD writes.
- Profile with `.books/profile-session` marker and inspect `.books/startup.log` after clean exit for elapsed time, CPU, peak RSS, block I/O and context switches. Block counts are not SD bytes. Measure battery separately and compare matched brightness/audio settings.

These are design constraints and source limits, not measured whole-process RAM or battery results.

## Validation and release

Host GCC/Clang CTest, Q2 MIPS32r2 O32 FP64 hard-float NaN-2008/ABS-2008 build, ELF checks, QEMU regressions and ZIP/checksum gates precede publication. Physical-device validation remains required for a stable designation: firmware revision, SD, wheel/touch, screen rotation, suspend/forced exit, resume anchors, TLS, font switching, large-library latency, RAM/CPU/battery and storage failures. See [roadmap](ROADMAP.md) for unfinished tests and [build guide](releasing.md) for commands.

## Known limitations

Initial DRM-free legacy MOBI/PalmDOC PRC import supports uncompressed and PalmDOC-compressed records, UTF-8 and Windows-1252 text, basic HTML formatting, and a 1 MiB decoded-text limit. The importer reuses the cached EPUB reader and rejects DRM, KF8-only, Huffman/CDIC and unsupported Palm databases. EXTH metadata, declared cover thumbnails, bounded PNG/JPEG recindex illustrations, heading/page-break chapters and legacy filepos links are implemented in the development branch; wider format compatibility and validation remain pending; publisher CSS/font support is bounded; complex scripts and regional Han forms depend on font coverage. Full fidelity, hardware suspend and battery/long-session memory have not been comprehensively measured. Q2 Pod's App Store/name integration may still require upstream changes. No claim of complete Unicode grapheme segmentation.

## Attribution

CrossPoint Reader (MIT, with separately licensed dependencies/fonts), Q2 Pod firmware integration, and existing third-party notices in `crosspoint-core/licenses/`. Preserve original notices and audit dependencies/font licenses before distribution.


## Reading statistics, streaks and goals

From **Home → Options**, scroll to the second page or tap its page indicator.
**Reading statistics** shows today, the last seven local calendar days, the
seven-day daily average, and per-book lifetime totals. **Streaks and goals**
shows today's minute and page targets and both streaks. Select a goal with
Centre or tap its row to cycle through the available targets, including Off.

- Active time counts only while the book page is shown and user input occurred
  within the last two minutes. Books pauses counting for overlays, screen off
  and suspend, and discards intervals interrupted by a clock jump. Screen and
  power state come from Q2 Pod's `/mnt/data/q2-state` on firmware that exposes
  it. On older firmware the inactivity and elapsed-time checks still apply.
- A page earns credit after at least two active seconds when turned forward.
  The last page also earns credit when leaving the book with Back and marks the
  book finished once. Previously credited text ranges do not count again after
  a backward turn, restart, or repagination. For pages without usable source
  offsets, Books credits page numbers in the first layout used for that
  chapter. Past reading positions and manual status edits do not add history.
- A local day qualifies after one active minute or one credited page. If
  today has yet to qualify, the current streak reports the run ending
  yesterday. History keeps the device's local date at the time of reading. Changing goals changes today's progress display without rewriting
  past activity or streaks.
- Versioned files `reading-statistics` and `reading-goals` stay in `.books`.
  An atomic replacement keeps the previous checkpoint if a write is
  interrupted. Statistics checkpoint once per minute while dirty and on clean
  exit. The reader never sends statistics or goal data over the network.
