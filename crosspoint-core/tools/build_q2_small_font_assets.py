#!/usr/bin/env python3
"""Generate real CrossPoint CPFONT v4 glyphs at 10/11 pt for Shanling Q2.

The 12 pt assets remain pinned to official CrossPoint release SHA-256 hashes
in fetch_q2_fonts.py. Smaller fonts must be *rasterized at their real point
sizes* with correct metrics; scaling existing 12 pt bitmaps corrupts shaping
and page layout. Source uses a pinned CrossPoint repository commit and its own
font converter, including its license files.
"""
from __future__ import annotations

import hashlib
import pathlib
import shutil
import subprocess
import sys
import tempfile

import yaml
from crosspoint_script_fonts import SCRIPT_FONTS

# Pinned upstream snapshot. Only the font build tooling and source-family
# definitions are used; no ESP32 firmware is compiled for the Q2.
CROSSPOINT_REF = "42d58daaabcf469a2e5e1ba871714fb9dcf51d29"
CROSSPOINT_URL = "https://github.com/crosspoint-reader/crosspoint-reader.git"
SIZES = (10, 11)
FAMILIES = {
    "NotoSansExtended": "sans",
    "NotoSerifExtended": "serif",
    "NotoSansCJKJP": "cjk",
    **{font: slug for slug, (font, _desc, _assets) in SCRIPT_FONTS.items()},
}


def run(*args: str) -> None:
    subprocess.run(list(args), check=True)


def main() -> int:
    if len(sys.argv) != 2:
        print("usage: build_q2_small_font_assets.py OUTPUT_DIRECTORY", file=sys.stderr)
        return 2
    target = pathlib.Path(sys.argv[1]).resolve()
    target.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="crosspod-small-fonts-") as work:
        workspace = pathlib.Path(work)
        upstream = workspace / "crosspoint"
        run("git", "init", "-q", str(upstream))
        run("git", "-C", str(upstream), "remote", "add", "origin", CROSSPOINT_URL)
        run("git", "-C", str(upstream), "fetch", "--quiet", "--depth", "1",
            "origin", CROSSPOINT_REF)
        run("git", "-C", str(upstream), "checkout", "--quiet", "--detach", "FETCH_HEAD")

        scripts = upstream / "lib/EpdFont/scripts"
        config = yaml.safe_load((scripts / "sd-fonts.yaml").read_text())
        catalog = {item["name"]: item for item in config["families"]}
        missing = sorted(set(FAMILIES) - set(catalog))
        if missing:
            raise RuntimeError("Upstream fonts missing: " + ", ".join(missing))
        families = []
        for name in FAMILIES:
            source = dict(catalog[name])
            source["sizes"] = list(SIZES)
            families.append(source)

        subset = workspace / "crosspod-sizes-10-11.yaml"
        subset.write_text(yaml.safe_dump({"families": families}, sort_keys=False))
        built = workspace / "fonts"
        # CJK font rasterization is much heavier than Latin. Bound parallelism
        # and give each family enough time without raising peak Q2 RAM usage.
        run(sys.executable, str(scripts / "build-sd-fonts.py"),
            "--config", str(subset), "--output-dir", str(built),
            "--jobs", "2", "--timeout", "2400")

        checksums = []
        for name, slug in FAMILIES.items():
            for size in SIZES:
                source = built / name / f"{name}_{size}.cpfont"
                if not source.is_file():
                    raise RuntimeError(f"Missing freshly rasterized font: {source}")
                data = source.read_bytes()
                if len(data) < 10_000 or data[:8] != b"CPFONT\x00\x00":
                    raise RuntimeError(f"Invalid CPFONT v4 generated: {source}")
                dest = target / f"{slug}-{size}.cpfont"
                shutil.copyfile(source, dest)
                digest = hashlib.sha256(data).hexdigest()
                checksums.append(f"{digest}  {dest.name}")
                print(f"Generated CrossPoint {name} {size}pt: {len(data)} bytes")

        (target / "crosspod-fonts-10-11.sha256").write_text(
            "\n".join(checksums) + "\n")
        shutil.copyfile(built / "LICENSE", target / "CrossPoint-font-licenses.txt")
        # Validate these sizes exist for *all* 14 scripts and both primary
        # families, or fail the package rather than silently drop glyphs.
        assert len(checksums) == len(FAMILIES) * len(SIZES)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
