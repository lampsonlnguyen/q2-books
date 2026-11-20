# Shanling Q2 ABI release gate — CrossPod

## Why the generic Ubuntu MIPS build is not device-ready

Shanling Q2 (Ingenic X2000) uses MIPS32r2, little-endian O32,
**FP64**, **hard float**, **NaN-2008**, ABS-2008, and glibc 2.28.
The standard Ubuntu `mipsel-linux-gnu-g++` runtime libraries in
GitHub Actions were built for legacy NaN. Compiling CrossPod sources
with `-mnan=2008` succeeds but **linking fails** with:

```
linking -mnan=2008 module with previous -mnan=legacy modules
```

This is a real ABI mismatch, not a transient CI or formatting issue.
It must not be bypassed with linker `--no-warn-mismatch`, ELF header
patching, or fake `readelf` output.

## Release requirements

1. Obtain a complete Q2-compatible NaN-2008 toolchain and sysroot,
   including matching C/C++ runtime and startup objects. The stock
   firmware runtime/rootfs can inform this but is not necessarily
   a complete development sysroot.
2. Compile **every** object (CrossPoint, Expat, zlib, C++ runtime) for
   MIPS32r2 O32 FP64 hard-float NaN-2008 ABS-2008.
3. Link against **matching** runtime libraries. For dynamically linked
   output, the only acceptable device loader is
   `/lib/ld-linux-mipsn8.so.1`; glibc symbols must be <= 2.28.
4. Run `crosspoint-core/tools/check_q2_elf.py` on the final stripped
   `.crosspod/crosspod-engine` to reject wrong architecture, FP ABI,
   NaN mode, interpreter, or runtime version.
5. Run CrossPod's EPUB, multilingual, image-rendering and font tests
   under MIPS QEMU using the appropriate dynamic sysroot if needed.
6. Package `.crosspod/q2-crosspod` as a POSIX `#!/bin/sh` launcher
   and `.crosspod/crosspod-engine` as the checked MIPS ELF; preserve
   `q2app` with `game=1` and `row=library`.
7. Only then promote the diagnostic archive to a Q2 hardware
   candidate. The final step is validation by a device owner.

## Hardware startup diagnostics

The Q2 Pod game launcher runs `.crosspod/q2-crosspod` directly with
`execl`. The POSIX script checks for the executable and writes
`.crosspod/startup.log` before execing the reader. It keeps up to
64 KiB per startup log and rotates the old log, with last errors in
`startup.previous.log`. If there is no log, the shell launcher was
not invoked or the card could not be written. If there is a log with
`not found`, `Exec format error`, or permission failures, it is
an executable/ABI/SD-mount problem. If it logs reader startup,
investigate framebuffer and input handoff next.

## Reproducible current checks

- `CrossPod card launcher diagnostics CI`: Q2 shell entrypoint, stdout/stderr
  log, missing engine error and log rotation.
- `CrossPoint Core CI`: host rendering/unit tests.
- `CrossPod Shanling Q2 FP64/NaN-2008 card app`: **release is blocked**
  until the linker has compatible runtime libraries; this failure
  is deliberate and must not be mistaken for a finished build.

The earlier generic MIPS static build remains a debugging baseline,
not a Q2-ABI-verified release. Do not flash firmware for any of these
steps.
