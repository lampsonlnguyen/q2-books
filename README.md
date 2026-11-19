# CrossPod — TXT + EPUB reader for Q2 Pod

CrossPod is a lightweight, native **Linux/MIPS book reader** for [DiamondBond's Q2 Pod](https://github.com/DiamondBond/q2-pod) on the Shanling Q2. Its goal is to adapt reading ideas and portable components from [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) to the Q2, without running ESP32 firmware or replacing Q2 Pod.

**Development status:** native TXT reader and experimental EPUB ZIP/DEFLATE + XHTML text extraction have been implemented. CI and physical Q2 performance **have not yet been verified**. This is not a full CrossPoint EPUB layout engine.

## Book formats

| Format | Current support |
| --- | --- |
| `.txt` | Basic plaintext display and page navigation |
| `.epub` | Unencrypted EPUB/ZIP using stored or DEFLATE-compressed `.html` / `.xhtml` chapters; simplified text-only extraction |
| EPUB images, CSS, rich layout, TOC | Not yet |
| DRM-locked EPUB | Not supported |
| Vietnamese, other non-ASCII scripts | Font/UTF-8 shaping not yet implemented |

The current EPUB parser does **not** yet use the OPF spine, so chapters are displayed in ZIP entry order (which can differ from reading order). Some valid EPUBs will not render correctly. Individual compressed XHTML files must be no larger than 512 KiB and uncompressed XHTML files no larger than 256 KiB. The extracted reading text is limited to 256 KiB. ZIP64 and encrypted entries are not supported.

## Build and install

On Ubuntu / WSL:

```sh
sudo apt-get install clang lld make zip
git clone https://github.com/lampsonlnguyen/q2-crosspoint
cd q2-crosspoint
make
```

Extract `dist/crosspod.zip` at the **SD card root**. The package contains:

```text
/.crosspod/q2app
/.crosspod/q2-crosspod
/.crosspod/book.txt
```

Copy your book to **one** of these paths:

- `/.crosspod/book.epub` — tried first
- `/.crosspod/book.txt` — fallback when there is no valid EPUB

Restart Q2 Pod, then look for **CrossPod directly in Library**, not Games. Q2 Pod currently does not expose an app-manifest setting for installing a card app inside **Library → Books**. That requires a Q2 Pod firmware menu modification. If the app is not discovered, remember Q2 Pod's card-app discovery limit; temporarily remove unused apps. This ZIP does not modify firmware.

## Controls

- Wheel: page backward/forward
- Previous/Next: page backward/forward
- Center: next page
- Touch: left/right half to go backward/forward
- Back: exit

## Design

The reader uses the native Q2 framebuffer, a lightweight software-drawn paper-style page, and the existing Q2 card-app input socket approach. Unlike a game, redraw is only requested when changing pages, reducing background work.

Core files:

- `src/platform.c` — MIPS system calls, framebuffer, input and file IO
- `src/reader.c` — TXT indexing, pagination, navigation
- `src/epub.c` — EPUB ZIP directory lookup and XHTML-to-text conversion
- `src/sinfl.h` — small independent DEFLATE decoder by Micha Mettke
- `src/render.c` — plain book-page UI
- `tests/test_reader.c` — portable TXT/EPUB parser smoke test

## Next milestones

1. Validate CI compilation and real device reading.
2. SD card library listing for multiple books, folder browsing and saved reading position.
3. Correct EPUB OPF manifest and spine ordering, efficient chapter paging and CSS-aware layout.
4. Unicode text and user-selectable readable fonts, including Vietnamese diacritics.
5. Optional TOC, bookmarks and KOReader progress sync.

## Licensing and credits

CrossPod's application code and Q2 bridge derive from the GPL-3.0-or-later native Q2 examples in [q2-games](https://github.com/lampsonlnguyen/q2-games), and are distributed under GPL-3.0. The adapted font originated from that Q2 example and retains its original licensing obligations. `src/sinfl.h` is Micha Mettke's MIT/public-domain code, with notice retained in the file. CrossPoint Reader itself is separately maintained; its original code is generally MIT-licensed, while some components have additional terms. No claim of official CrossPoint affiliation or full CrossPoint feature compatibility is made.
