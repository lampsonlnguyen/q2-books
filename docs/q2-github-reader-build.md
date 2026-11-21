# CrossPod Q2 hardware alpha (GitHub Actions)

Build workflow: `.github/workflows/q2-reader-card.yml`.
Source branch: `crosspod-reader-main-integration`.
The workflow can also be launched with **Actions → CrossPod Q2 reader hardware alpha → Run workflow**.

## Download and install
1. Open the most recent **successful** workflow run.
2. Download the **crosspod-q2-reader-alpha** artifact, then extract its outer GitHub artifact ZIP.
3. Inside is `crosspod-q2-reader-alpha.zip`. Extract this second ZIP to the **root of your Q2 microSD card**.
4. Confirm `.crosspod/q2app`, `.crosspod/q2-crosspod`, `.crosspod/crosspod-engine`, `.crosspod/fonts/sans-16.cpfont`, `.crosspod/fonts/serif-18.cpfont`, and `Books/vietnamese-prose.epub` exist on the card.
5. Start **Library → CrossPod** in Q2 Pod. The CrossPod menu should discover the included sample EPUB. While reading, press Centre to open Reading appearance and change Sans/Serif and 12/14/16/18 pt.
6. Test wheel forward/backward, Centre, touchscreen, and Back-to-exit. Do not overwrite stock Books or install this as firmware.

This is a removable Q2 Pod **card app**. There is no `update.tar` or firmware flashing involved.

## Verification performed by CI
- Downloads **Q2 Pod v1.8.0 update.tar**, verified with GitHub's published SHA-256 checksum. This is used solely to obtain the target glibc/loader and C++ runtime for linking and emulation, and is **not** shipped in the output.
- Builds original CrossPoint's executable for MIPS32r2, O32, hard-float FP64, NaN-2008.
- Uses pinned zlib 1.3.1 and Expat 2.6.4 sources.
- Inspects ELF ABI, dynamic loader, and glibc symbol requirements.
- Runs EPUB validation, multi-page traversal, Vietnamese rasterization, embedded PNG rasterization, and card picker preview through QEMU with the Q2 runtime.
- Uploads card ZIP only if **all** gates pass.

## Experimental limitations
- Physical Q2 hardware validation is still required; QEMU is not a framebuffer/touchscreen replacement.
- The XHTML collector is transitional, not the entire upstream CrossPoint chapter/Section rendering pipeline.
- Full CJK/Arabic/Hebrew fonts, complex CSS and broader image support are not yet complete.
- The ZIP now includes the original CrossPoint extended Sans and Serif `.cpfont` families at 12, 14, 16 and 18 pt (8 files). Every font is SHA-256 checked against the official CrossPoint font release. Under QEMU the installed 16 pt Sans and 18 pt Serif produced distinct page rasters without fallback. Full language coverage depends on each font’s glyph set.
- If opening fails, retrieve `.crosspod/startup.log` from your SD card; it records loader errors.

## Rebuilding without WSL
This is entirely hosted on GitHub Actions: no local MIPS compiler, sysroot, or firmware ZIP is needed. The workflow is fail-closed; failures show logs and do not upload a purported working reader.
