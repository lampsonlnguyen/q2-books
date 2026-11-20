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
