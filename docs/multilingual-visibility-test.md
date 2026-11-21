# CrossPod multilingual font-visibility EPUB

Every Q2 test-build ZIP includes:

`Books/CrossPod-Multilingual-Visibility-Test.epub`

The reproducible generator is:

`python3 crosspoint-core/tests/make_language_visibility_epub.py OUTPUT.epub`

To verify its structure without rewriting the file:

`python3 crosspoint-core/tests/make_language_visibility_epub.py OUTPUT.epub --check`

## Purpose

This intentionally small, single-chapter EPUB has 30 **language samples**,
including every one of the 14 optional script families enumerated in
`crosspoint-core/reader/ScriptFonts.hpp`. It probes Latin (including
Vietnamese, French, German, Spanish, Portuguese and Turkish), Greek,
Cyrillic, Chinese Simplified/Traditional, Japanese, Korean,
Devanagari (Hindi, Marathi, Nepali), Bengali/Assamese, Gujarati,
Punjabi/Gurmukhi, Kannada, Malayalam, Odia, Sinhala, Tamil, Telugu,
Arabic, Hebrew, and Thai.

Languages with uppercase/lowercase letters show both; scripts without
case show a representative set of their letters, combining marks, and
samples. This is *not* a Unicode-codepoint exhaustive test (which would
not fit in a short ebook). The generator labels configured CrossPod
fallback scripts as `[fallback]` and additional coverage probes as
`[probe]`. In particular, Thai/Greek/Cyrillic are visibility probes,
not an assertion that CrossPod currently ships specialized fallback
fonts for every character.

## Test on the physical Q2

1. Install any Q2 test build from the GitHub Actions hardware-alpha
   artifact. The book is installed alongside the other books in the
   card's `/Books` folder. Alternatively, copy only the EPUB there.
2. Open CrossPod → Browse files → **CrossPod Multilingual Visibility Test**.
3. Inspect each paragraph for characters that turn into squares, missing
   accents, missing letters, incorrectly placed vowel marks, incorrect
   joining, or reversed right-to-left order.
4. Scroll through all pages forward and backward. The text must continue
   to render after prolonged click-wheel input.
5. Report *language/paragraph name*, the affected characters, the
   original font setting, and optionally a photograph or device log.
   This helps distinguish glyph-coverage failures from shaping problems.

## What automated testing proves

- The EPUB is structurally well formed, UTF-8 encoded, reproducible,
  and consists of an EPUB-conformant uncompressed first `mimetype` entry.
- Each named language sample exists in the sole XHTML chapter.
- The Q2 MIPS application opens this book using the original CrossPoint
  Section path, after the pinned cpfont bundle has been installed.
- The Q2 app ZIP includes the same generated EPUB. This is implemented
  in `crosspoint-core/tools/build_q2_github.sh`, used by the Q2 hardware
  alpha workflow.

**Passing automation does not prove every glyph is visible or correctly
shaped.** Visual validation is still needed on the actual LCD.
