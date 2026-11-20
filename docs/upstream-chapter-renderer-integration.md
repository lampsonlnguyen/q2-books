# Original CrossPoint XHTML renderer integration checkpoint

Upstream: `crosspoint-reader/crosspoint-reader`, master commit
`42d58daaabcf469a2e5e1ba871714fb9dcf51d29`.
Original source added without rewriting:

- `crosspoint-core/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.h`
- `crosspoint-core/lib/Epub/Epub/parsers/ChapterHtmlSlimParser.cpp`

The interface compiler regression target is
`crosspoint_chapter_parser_interface_tests`.

## Current boundary

The Q2 `crosspod-reader` still uses its temporary Expat-based
`HtmlCollector` and only preserves paragraph text. Importing the
original parser is **not** equivalent to connecting it to the reader.

To activate it, the implementation needs the original `Epub` object
and `Section` page lifecycle, `Page` image/link elements and serialization,
`ImageBlock` and image converter/probe backends, stylesheet
resolution, and filesystem/cache adapters. The current POSIX
`platform/posix/Epub.h` is only an OPF compiler shim and **cannot**
stand in for the upstream `Epub` implementation.

## Integration gates

1. Import only the upstream reader-domain dependencies (not ESP32 firmware).
2. Compile and **link** the original `ChapterHtmlSlimParser.cpp` with
   `Section`/`Page`, font/render, CSS, ZIP and Q2 POSIX HAL.
3. Replace `HtmlCollector` in `reader/crosspod_reader.cpp` only when that
   linked path can open a real EPUB and emit its pages.
4. Compare pages from a Vietnamese and a styled EPUB against upstream
   reference renders, covering headings, emphasis, margins, images, and
   page boundaries.
5. Run GCC/Clang and MIPS QEMU EPUB tests before packaging the card app.
6. On Q2 hardware check wheel/touch response, framebuffer restoration,
   font fallback, peak RAM, CPU and battery impact.

No firmware changes, and no replacement of the original CrossPoint
layout algorithms, are part of this work.
