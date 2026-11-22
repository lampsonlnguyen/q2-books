# Integrating CrossPoint's reader with Q2 Pod Books

Reviewed Q2 Pod: [DiamondBond/q2-pod `patch/books.c`](https://github.com/DiamondBond/q2-pod/blob/main/patch/books.c) and `patch/navigation.c`, plus `patch/addon.c`. CrossPod is a **separate Linux/MIPS reader application**, not a Q2 Pod game or its old book renderer. The existing Q2 Pod Books shelf is the entry point.

## Existing Q2 Pod implementation

- **Shelf:** `books_open(root)` enumerates `/mnt/mmc/Books` and one level of subfolders into a sorted list; up to 500 books. Each row triggers `open_book` then `to_reader`.
- **EPUB:** `book_convert` reads `META-INF/container.xml`, the OPF spine and XHTML from ZIP, strips XHTML to plain text and joins chapters using form-feed page breaks. A worker thread writes `/mnt/mmc/.books/<path-hash>.txt`. Conversion has a **64 MiB** text cap and per-entry limits.
- **TXT:** Opened as plain text without conversion. Both formats use a **32 KiB sliding read buffer** and `book_page` + `book_back` to paginate around an offset.
- **Rendering:** `books_paint` uses the default stock font via `canvas_set_font(..., NULL, 20)` and `canvas_draw_text`; fixed 10 rows, ~26 px pitch. No EPUB CSS, images, headers or rich typography remain after text extraction.
- **Controls:** `books_key` consumes click-wheel next/previous; Centre toggles a caption; Return goes back to the Books list.
- **Progress:** `/mnt/data/ringnav-books` stores the 64 most recent (FNV path hash, byte offset) pairs. CrossPoint will need its own position/section scheme; stock offsets are not directly convertible.

## Desired integration

```text
Q2 Pod Library > Books (KEEP)
       │
       └── user selects /mnt/mmc/Books/<file>.epub or .txt
                     │ absolute path (UTF-8)
                     v
           Standalone CrossPod Linux/MIPS process
                     │
                     ├── ORIGINAL CrossPoint Epub + OPF spine + TOC + CSS
                     ├── ORIGINAL CrossPoint ParsedText + Page + fonts + caches
                     ├── ORIGINAL CrossPoint TXT-to-XHTML pipeline
                     ├── POSIX file/cache/clock/power adapters
                     ├── LCD framebuffer display adapter (no e-ink driver)
                     └── wheel + touchscreen adapter (no Q2 game runtime)
```

**Contract:** Pass selected book's complete UTF-8 absolute path as `argv[1]`; paths accepted by the reader must be under `/mnt/mmc/Books/`, with `.epub` or `.txt` suffix. `crosspoint-core/platform/q2/Q2BookHandoff.hpp` validates this launch format. Before opening files, resolve symlinks and confirm the resolved path remains in `/mnt/mmc/Books/`; the current helper only checks lexical traversal.

**Ownership:** While the CrossPod process is active, Q2 Pod suspends painting and forwards click-wheel, buttons and touch to the process via a dedicated IPC socket. CrossPod owns pagination, drawing and reading settings. On Back/exit, Q2 Pod resumes the Books shelf with the selected entry highlighted. No UI redraw by two processes at once.

**Power:** Q2 Pod firmware owns device power, display dimming, sleep and wake. CrossPod checkpoints reading progress and saves pending state on graceful termination. When relaunched, it restores the saved reading position.

**Fallback:** Until the complete CrossPoint-based MIPS reader is present and verified, Q2 Pod's `to_reader` must continue opening the current built-in reader. Do not route real books to a development-only host library or an unimplemented executable.

## Status and next tasks

Already done in CrossPod:
- Original CrossPoint `Utf8`, `TxtToHtml` and now `ContainerParser` (with system Expat) compile as distinct Linux host components; Q2 path validation and wheel/touch/LCD adapters have host tests.
- No CrossPoint ESP32 firmware, bootloader, FreeRTOS application or e-ink driver is part of the compiled deliverable.

Still needed before enabling the shelf handoff:
1. Port original CrossPoint `ZipFile`, OPF manifest/spine, chapter parsing, CSS and `ParsedText` layout to POSIX/MIPS.
2. Port original `GfxRenderer`/fonts to the Q2 LCD, including Vietnamese NFC and NFD display tests.
3. Implement native Linux executable, Q2 input socket/display ownership and storage/cache layer; cross-compile and run on device.
4. Hook the existing Q2 Pod Books row to the executable **with a safe fallback**, and add integration tests covering opening, returning, position saving and sleep behavior.

**Do not port the stock Q2 Pod reading algorithm:** `books.c` is an integration reference only. **Do not port ESP32 firmware:** only CrossPoint reader source and its portable dependencies belong in CrossPod.
