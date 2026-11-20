CrossPod Q2 Pod CARD APP (illustrated EPUB, Q2 ABI diagnostics)

INSTALLATION: Unzip crosspod-card-app.zip to the root of the Q2 SD card.
The exact file paths must be:
 /mnt/mmc/.crosspod/q2app
 /mnt/mmc/.crosspod/q2-crosspod    (POSIX shell launcher)
 /mnt/mmc/.crosspod/crosspod-engine (MIPS reader ELF)
 /mnt/mmc/Books/YourBook.epub

On supported Q2 Pod firmware, choose Library > CrossPod.
If your Q2 Pod version does not support manifest row=library, look under Games.
This is a standalone card-app launcher. DO NOT flash any firmware.
Q2 Pod does not need the optional CrossPod Books hook for this card app.

Opening the app without arguments shows a book selector. Turn the wheel,
press Centre to open, use wheel or left/right touchscreen to turn pages.
Back from reading returns to the CrossPod shelf; Back on the shelf exits.

Only EPUB files are currently handled. It lists the SD root Books folder and
one subfolder level. TXT books, JPEG, and complex EPUB layouts are not yet supported.
Current illustrated EPUB preview supports bounded non-interlaced RGB/RGBA PNGs.

Q2 device execution is NOT verified. The new build targets the Shanling Q2's
MIPS32r2 O32 FP64 hard-float, NaN-2008 and ABS-2008 ABI, with a strict ELF
pre-release check and static MIPS QEMU tests. Generic MIPS QEMU does not prove
physical device compatibility. Do NOT flash firmware or replace system libraries.

STARTUP DIAGNOSTICS:
Q2 Pod's game card executes .crosspod/q2-crosspod, a #!/bin/sh wrapper. The
wrapper logs to .crosspod/startup.log before attempting to exec crosspod-engine.
This log records kernel details, binary presence, free memory and reader stderr.
If the engine ELF fails to start, the shell's error is captured here.
The log is capped at approximately 64 KiB per attempt by rotating the previous
file to startup.previous.log. Logging failures do not prevent starting the app.
After testing on Q2, power off before removing the SD card, then send
.crosspod/startup.log and (if present) startup.previous.log. If no log exists,
then Q2 Pod did not reach the launcher (or the card was not writable); that
narrows the cause to the Q2 Pod fork/exec, script permission, or SD mount.
For direct terminal tests the reader is .crosspod/crosspod-engine, not the launcher.

This build uses CrossPoint's original EPUB ZIP/OPF/spine, ParsedText, TextBlock
and GfxRenderer; XHTML collection is provisional and can lose styling.

Reading-position resume (development branch): CrossPod now stores a small
per-book progress file under /mnt/mmc/.crosspod/ after an interactive page
turn. Reopening the same EPUB restores the last chapter and page. Progress
is ignored when the EPUB file size or modification time changes. The saved
page is clamped to the currently paginated chapter; this transitional format
is NOT CrossPoint's native progress.bin and is not KOReader sync compatible.
The CLI --validate, --render and --walk modes do not write progress.

TYPOGRAPHY MENU (experimental font builds):
While reading, press Centre or tap the top 45 pixels to open Reading appearance.
Turn the wheel to choose Font or Size; Centre cycles the selected value.
Back closes the menu. Choose Sans or Serif and 12, 14, 16, or 18 pt.
CrossPod repaginates the current chapter and saves your choices in
/mnt/mmc/.crosspod/typography.conf. The book's page number may shift when
font size changes; location anchoring is still a follow-up.

The card app CI downloads exactly two original CrossPoint .cpfont families:
NotoSansExtended and NotoSerifExtended (each in 12/14/16/18pt), supplied
from the official crosspoint-reader/crosspoint-fonts sd-fonts-m1-b4 release.
They're installed under .crosspod/fonts/ in the card-app ZIP.
An older ZIP without these files supports only built-in Sans 12.
Font loading or repagination failure is shown in the menu and retains the
previous setting. Full language coverage depends on each font's glyph set.
Original font notices and release: https://github.com/crosspoint-reader/crosspoint-fonts
