# CrossPod — direct CrossPoint Reader port to Shanling Q2

**This is the real CrossPoint engine migration branch. It is not yet a functional Q2 e-reader.**

The original CrossPod prototype is preserved on the [main branch](https://github.com/lampsonlnguyen/q2-crosspoint/tree/main). This branch **removes** its custom C EPUB extractor, text renderer, bitmap font reader and Q2 game-style runtime.

The new architecture retains CrossPoint's source-level EPUB/TXT parsing, layout, fonts, page caches, activities and settings, and adapts only the hardware interfaces to the Q2's Linux display, click wheel, touchscreen and SD card. See [the technical plan](crosspoint-core/README.md).

## What is actually ported now

- Original CrossPoint Unicode/NFC code, unmodified and with original license
- A standalone click-wheel and touch-event translator independent of the Q2 Pod card-app framework
- A basic LCD output adapter for CrossPoint-style monochrome rasters and optional warm/cool palettes
- A reproducible checkout script for the **complete pinned CrossPoint upstream source**
- CMake host compilation and test workflow for these components

**Not yet done:** CrossPoint EPUB/TXT activities, storage, font system, GfxRenderer and power HAL on MIPS Linux. There is no replacement `crosspod.zip` from this branch yet. A genuine build requires porting the FreeInk/Arduino/FreeRTOS integrations, not pretending CrossPoint's ESP32 firmware can run directly on Linux.

## Build currently ported source tests

```bash
sudo apt-get update && sudo apt-get install -y git g++ cmake make
git clone -b crosspoint-core-port https://github.com/lampsonlnguyen/q2-crosspoint.git
cd q2-crosspoint
make
```

`make` tests the upstream CrossPoint Unicode core and Q2 adapter functions **only**. It does not produce a device binary.

For the complete original CrossPoint code at the reviewed revision:

```bash
make upstream
```

It appears in `crosspoint-core/upstream/` and is intentionally ignored by Git.

## Device integration design

- Click wheel: previous/next scrolling/page turning via CrossPoint's input action model
- Touch: gesture and press events using CrossPoint's UI and reader menu
- Display: CrossPoint's layout and rasterization → adapted LCD output
- Storage: standard POSIX files under `/mnt/mmc/Books` with original CrossPoint cache format
- Reading preferences: CrossPoint's settings, with Q2-specific color/brightness controls
- System lifecycle: Q2 Linux idle dim, sleep, restore; not an ESP32 deep-sleep implementation

Q2 Pod, if installed, may offer a Library launcher but must not be used as the e-reader runtime or rendering framework.

## Licensing

The code copied unmodified from CrossPoint is governed by CrossPoint's original MIT license and associated third-party notices: see [LICENSE-CROSSPOINT](crosspoint-core/LICENSE-CROSSPOINT). The previous GPL reader code remains on the separate `main` branch.
