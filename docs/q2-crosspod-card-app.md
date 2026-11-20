CrossPod Q2 Pod CARD APP (text-only EPUB development build)

INSTALLATION: Unzip crosspod-card-app.zip to the root of the Q2 SD card.
The exact file paths must be:
 /mnt/mmc/.crosspod/q2app
 /mnt/mmc/.crosspod/q2-crosspod
 /mnt/mmc/Books/YourBook.epub

On supported Q2 Pod firmware, choose Library > CrossPod.
If your Q2 Pod version does not support manifest row=library, look under Games.
This is a standalone card-app launcher. DO NOT flash any firmware.
Q2 Pod does not need the optional CrossPod Books hook for this card app.

Opening the app without arguments shows a book selector. Turn the wheel,
press Centre to open, use wheel or left/right touchscreen to turn pages.
Back from reading returns to the CrossPod shelf; Back on the shelf exits.

Only EPUB files are currently handled. It lists the SD root Books folder and
one subfolder level. TXT books and complex EPUB CSS/images are not yet supported.

Q2 device execution is NOT verified. Compiled for generic MIPS32r2 Linux with
static libraries and validated under QEMU. If the app closes or opens to black,
capture device diagnostics instead of replacing firmware or system libraries.

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
