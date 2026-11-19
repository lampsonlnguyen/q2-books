# CrossPod / Q2 Pod Books runtime integration

**Status:** Source-level launch/input/framebuffer transport implemented.
**Not yet installable as an EPUB reading app:** CrossPoint's full text layout, font
shaping and reader activities have not been wired into an executable, and no
MIPS application has been validated on the Shanling Q2.

## Component ownership

- Q2 Pod retains the existing Books shelf and handles discovery under
  `/mnt/mmc/Books/`. No firmware from CrossPoint's ESP32 system is used.
- When installed, Q2 Pod's optional Books hook executes
  `/mnt/mmc/.crosspod/crosspod <absolute-book-path>`.
- CrossPod owns `/dev/fb0` only during its process lifetime.
- Q2 Pod stops painting while the reader is running and restores the shelf
  when CrossPod exits.
- If the executable is absent or fails to execute, the stock EPUB/TXT reader
  remains available. The opt-in launcher is in
  [q2-pod fork PR #1](https://github.com/lampsonlnguyen/q2-pod/pull/1).

## Input protocol

Q2 Pod sends Unix domain datagrams to `/tmp/q2-crosspod.input.sock`:

| Wire message | Meaning |
| --- | --- |
| `k d 218` | Centre pressed |
| `k u 218` | Centre released |
| `k u 170` | Return released (exit/reader menu) |
| `w -45` | 45 raw wheel units (Q2 wheel ~200 units per revolution) |
| `t d 50 200` | Landscape touch down at (50, 200) |
| `t m 50 201` | Touch movement |
| `t u 50 201` | Touch release |

`Q2SocketInput` opens a nonblocking `AF_UNIX/SOCK_DGRAM` receiver,
validates event types and coordinates, and exposes `PhysicalEvent` to the
`Q2Controls` event translator. A reader should poll this socket only while
idle and render only when state/page changes. **The receiver exists, but the
full reader event loop and its actual page engine are still missing.**

Q2 Pod uses its own raw-wheel accumulation. Keep its sensitivity policy
consistent with the system setting before declaring wheel behavior complete.

## Display

The Q2's native framebuffer is **320 × 375 × 32-bit**, with two pages at
`/dev/fb0`. The UI's logical reading surface is **375 × 320**. The mapping is

```text
physical_x = 319 - logical_y
physical_y = logical_x
```

`Q2Framebuffer` uses the Linux framebuffer ioctl/mmap API, writes only the
hidden page, pans the display after presenting, and restores the original
page on exit. It requires the native geometry and 32-bit RGB layout to match
before drawing. `Q2Display` converts an existing 1-bpp CrossPoint-style page
into ARGB, and `Q2PagePresenter` bridges the two adapters using a ~480 KiB
scratch buffer. **The actual CrossPoint glyph and page renderer is not yet
connected to that pipeline.**

## Tests

On a Linux host:

```bash
sudo apt-get install -y cmake g++ make libexpat1-dev zlib1g-dev python3
cmake -S crosspoint-core -B build/crosspoint-core -DCMAKE_BUILD_TYPE=Release
cmake --build build/crosspoint-core -j2
ctest --test-dir build/crosspoint-core --output-on-failure
```

Tests cover ZIP/OPF/spine caching, socket datagrams and input parsing,
logical-to-native orientation and adapter failures. They **cannot** prove
Q2 hardware behavior, power/sleep handling, fidelity, or font coverage.

## Outstanding integration gates

1. Port original CrossPoint `Epub` chapter XHTML/CSS layout, `ParsedText`,
   `Page`, font families and reader activities onto POSIX Linux.
2. Produce a Q2 MIPS32 executable named `crosspod` with that engine wired
   to `Q2SocketInput` and `Q2PagePresenter`.
3. Validate font coverage, complex-script shaping/RTL/CJK, images, CSS, EPUB
   spine navigation and reading-position persistence with real books.
4. Verify USB, sleep/wake, click-wheel acceleration, touchscreen, CPU/RAM,
   and framebuffer ownership on Q2 hardware.
5. Only after those pass, package `.crosspod/crosspod` and enable/merge the
   Q2 Pod Books hook for a device-installable beta. Until then do not
   advertise the Linux development libraries as Q2-installable.

## References

- [Q2 Pod native Books implementation](https://github.com/DiamondBond/q2-pod/blob/main/patch/books.c)
- [Q2 Pod frame-buffer reference](https://github.com/DiamondBond/q2-pod/blob/main/patch/video.c)
- [CrossPoint original reader engine](https://github.com/crosspoint-reader/crosspoint-reader)
- [KOReader source](https://github.com/koreader/koreader)
