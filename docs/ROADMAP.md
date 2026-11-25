# Outstanding work

Only unfinished work is listed. Version labels identify feature groups, not guaranteed release numbers. Existing features are documented in [technical reference](TECHNICAL.md). The separate v1.13 font optimization is deferred; retain the existing embedded-font implementation. No new font-engine rewrite is planned.

### v1.11.0 — Cover Flow customization

- [ ] 1.11-M1 — Define bounded settings for cover size, spacing, reflections and animation speed; use firmware-style defaults.
- [ ] 1.11-M2 — Add themed wheel/touch settings controls and immediate visual preview; support reset to defaults.
- [ ] 1.11-M3 — Persist settings atomically and clamp invalid values; adapt geometry, hit testing and cache reach consistently.
- [ ] 1.11-M4 — Test extreme settings, rounded-screen clipping, light/dark themes, rendering cost and idle behavior.

**Acceptance:** Customization stays within safe geometry and memory limits and does not alter reader typography or system theme.

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

### In-book search follow-up

- [ ] Add TXT search using the existing reader pipeline; validate Unicode queries, chapter boundaries, skipped corrupt chapters, bounded batches and saved anchors.

### Regression and physical-device validation

- [ ] Alphabetical navigation: multilingual 4000-book shelves and rapid wheel input.
- [ ] Book details: missing/malformed metadata, cache invalidation and localization.
- [ ] Collections: moves, duplicates, empty groups and interrupted/read-only writes.
- [ ] Reading status: completion boundaries, legacy state, rereading and missing files.
- [ ] Recent carousel: empty/single/full history, themes, languages and startup latency.
- [ ] Series: multilingual names, standalone titles, mixed metadata, numeric sorting and cache invalidation.
- [ ] Existing embedded fonts: mixed scripts, fallback, pagination and memory churn; no new optimization branch required.
- [ ] Hyphenation: nested language scopes, mixed scripts, CJK/RTL, font changes and source-anchor resume.

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

### Scope

DRM-free MOBI/PRC, CBZ, custom reader themes and text-to-speech remain outside the active roadmap. Consider only after measuring cost and compatibility.
