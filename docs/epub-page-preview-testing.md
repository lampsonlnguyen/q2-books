# CrossPod actual-EPUB text-page integration and visual QA

**Status:** Verified host-side text-page smoke test only. Not a faithful EPUB
chapter reader or Q2-installable binary.

## Verified on 2026-11-20

- The original CrossPoint `ZipFile` extracts chapter XHTML from the generated
  `multilingual.epub` fixture.
- A **test-only Expat extractor** identifies paragraph/heading text from that
  XHTML. This deliberately does not replace the original production
  `ChapterHtmlSlimParser`.
- Original CrossPoint `ParsedText::layoutAndExtractLines` and `TextBlock::render`
  create line layout, and the original `GfxRenderer` with compressed
  `notosans_12_regular` font creates a 375×320 monochrome page.
- The PGM output is converted to grayscale PNG with Python stdlib and attached
  to the GCC workflow job as a downloadable artifact.
- GCC and Clang: **21/21 CTest tests passed**, including the original Liang
  hyphenation implementation and EPUB text-page preview.
- CI: https://github.com/lampsonlnguyen/q2-crosspoint/actions/runs/44781415336
- Rendered page PNG SHA-256:
  `795981e1c8faca3045b990b98cc91786de39c981a8da8aae21bb1291adcfeb08`
- Visual QA (375×320 preview): 8,742 black pixels; ink bounds
  `(15,20)-(357,146)`. This is a sparse three-paragraph fixture, not an
  entire real-world book chapter.

## Known visual defects and missing gates

**Multilingual font coverage is not complete.** Latin and Vietnamese text are
visible, but CJK and Arabic/Hebrew characters in the sample render as
replacement glyphs: the test currently registers only Noto Sans Latin.
CrossPoint's Unicode shaping tests do **not** establish coverage of all scripts
without a font registry and correct multilingual fallback.

The test-only XHTML extractor discards CSS, embedded images, nesting and
semantic styling; therefore the preview proves original CrossPoint
**text layout/rasterization**, not faithful EPUB presentation. The production
reader must use the upstream `ChapterHtmlSlimParser`, `Page`, `Section`,
image decoder/cache, font fallback and reader state.

No page-turn latency, RAM residency, battery, touchscreen, or Q2 framebuffer
ownership was measured on physical Q2 hardware.

## Reproduce the visual test on Linux

```sh
cmake -S crosspoint-core -B build/crosspoint-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/crosspoint-core -j2
ctest --test-dir build/crosspoint-core --output-on-failure
python3 crosspoint-core/tools/pgm_to_png.py \
  build/crosspoint-core/crosspod-epub-preview.pgm \
  build/crosspoint-core/crosspod-epub-preview.png
```

The source EPUB is a deterministic test fixture created by
`crosspoint-core/tests/make_epub_fixture.py`, not a public-domain novel.

## Next implementation gates

1. Link the full `Page` composition and original `ImageBlock` pipeline.
2. Build the original `ChapterHtmlSlimParser` + `Section` end-to-end.
3. Add multilingual font families/fallbacks and compare pages visually.
4. Produce a Q2 MIPS executable; test real device controls and performance.
