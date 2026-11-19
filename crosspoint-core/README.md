# CrossPod: genuine CrossPoint engine port

**Status: early, incomplete. Not a deployable reader.**

This directory begins a **source-level port of CrossPoint Reader** (https://github.com/crosspoint-reader/crosspoint-reader), NOT the earlier `src/reader.c` / `src/render.c` custom prototype. The old standalone implementation on `main` is retained only to avoid breaking the existing SD-card app while this migration is underway.

## Target

Run the actual CrossPoint EPUB/TXT engine and CrossPoint activity/state model on the Shanling Q2's MIPS32 Linux, maintaining upstream algorithms for:
- EPUB OPF spine, TOC, CSS, HTML parsing, text shaping, hyphenation and page caching
- TXT and EPUB readers, per-book resume/reading settings
- CrossPoint fonts and Unicode NFC, including Vietnamese
- Shared UI and reader menu behavior

Replace only device-specific layers:
- **HalDisplay / GfxRenderer backend:** CrossPoint's logical drawing commands to Q2's LCD framebuffer; no e-ink refresh policy
- **HalStorage:** POSIX files and `/mnt/mmc/Books`, with original CrossPoint cache layout
- **HalGPIO / MappedInputManager:** Q2 click-wheel and touch events to CrossPoint's `Previous`, `Next`, `Confirm`, `Back` and touch gestures
- **HalPowerManager:** Linux/Q2 LCD dim/suspend lifecycle, no ESP32 deep-sleep calls
- **System timing/network:** POSIX equivalents, no Arduino or FreeRTOS dependency

A Linux process requires an entry point and access to actual Q2 input/display hardware. The Q2 Pod card-app manifest remains only a *launch integration mechanism* if Library placement is desired. It must **not** be a reader, renderer, layout engine or input framework. The eventual reader should also run as a standalone Linux executable.

## Upstream source brought over so far

`lib/Utf8/Utf8.h`, `Utf8.cpp`, and `Utf8ComposeTable.h` were copied unmodified from CrossPoint. The original `lib/Txt/TxtToHtml.cpp` and `TxtToHtml.h` are also compiled unchanged using narrow POSIX replacements for Arduino Print, memory allocation, file helper and logging interfaces. The original EPUB `ContainerParser` and `XmlParserUtils` are now built against Linux Expat; Q2 book path handoff validation lives under `platform/q2/Q2BookHandoff.hpp`. These functions implement Vietnamese NFC normalization, including decomposed text frequently produced on macOS and inside EPUBs. `reference/` files are unmodified dependency-audit copies of the CrossPoint EPUB and layout code, **not yet compiled**. They identify what needs to be ported without replacing the algorithms.

CrossPoint original source is under MIT, with separately licensed dependencies and fonts (see CrossPoint's `LICENSE` and `licenses/`). The old CrossPod GPL prototype retains its own license. Porting new CrossPoint files must preserve upstream and third-party notices.

Source base inspected: CrossPoint commit `9ac3475733e677b7f61ba3f6e8ce41ce1b9b2967`. Do not mix sources from other revisions without rechecking API compatibility.

## Build current migrated core tests

On Ubuntu / WSL:

```sh
sudo apt-get install -y cmake g++ make libexpat1-dev
cmake -S crosspoint-core -B build/crosspoint-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/crosspoint-core
ctest --test-dir build/crosspoint-core --output-on-failure
```

These are **host-side component tests only**, including upstream TXT-to-XHTML conversion of Vietnamese UTF-8, upstream EPUB container parsing, and Q2 Pod Books path handoff. This branch does not yet produce a complete CrossPoint-based Q2 reader. The root `make` only runs CrossPoint-core and Q2-adapter host tests; it does **not** package the legacy prototype.

## Automated builds and releases

GitHub Actions `CrossPoint Core CI` runs for every push and pull request against `main`. It builds and tests the portable CrossPoint core with GCC and Clang and uploads a host-development ZIP (not Q2-installable).

To publish a development prerelease, create and push a tag named `core-vX.Y.Z` pointing to a commit on `main`:

```sh
git switch main
git pull --ff-only origin main
git tag core-v0.1.0
git push origin core-v0.1.0
```

The release workflow rebuilds and tests before publishing the archive and SHA-256 checksum. It labels the output **NOT Q2-installable**, because the current libraries are x86_64 host builds. These releases contain no ESP32 firmware, bootloader, FreeRTOS binary, or hardware drivers. A Linux/MIPS executable and app-installation archive will be added only when the actual CrossPoint reader is running on Q2.

## Next technical milestones

1. Reuse upstream CrossPoint `ZipFile`, `Epub`, `ContainerParser`, `ContentOpfParser`, `ParsedText`, `Page`, fonts, and font caches; implement required Linux HAL / Arduino-FreeRTOS compatibility interfaces.
2. Build the upstream EPUB/TXT core for MIPS Linux using the user's Shanling Q2 sysroot (C++ standard library, Expat, zlib and image codecs as needed).
3. Connect the original CrossPoint reader activities and reader settings to Q2 input, an LCD display backend and native Linux power control.
4. Verify Vietnamese (NFC **and** NFD), CSS, chapter order, resume state, and battery impact on physical Q2 hardware.
5. Replace the legacy `src/` application on `main` only after real book reading passes these tests.

For the existing Q2 Pod Books implementation and the cross-process integration contract see [Q2 Pod Books integration](../docs/q2pod-books-integration.md).

**Design rule:** preserve upstream CrossPoint reader logic and its update path. Do not write another bespoke EPUB parser or text layout engine.
