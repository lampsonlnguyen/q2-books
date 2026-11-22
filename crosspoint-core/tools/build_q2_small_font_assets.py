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
# CrossInk's named reader font catalog. The extra families are generated at
# all three 10/11/12pt sizes; the original two and script fallbacks reuse the
# officially SHA-pinned 12pt CrossPoint releases.
CROSSINK_REF = "9914146eeae7b46b300f475a16c32426fc02ec1f"
CROSSINK_URL = "https://github.com/uxjulia/CrossInk.git"
CROSSINK_FAMILIES = {
    "Bitter": "bitter",
    "Lexend Deca": "lexenddeca",
    "ChareInk": "chareink",
    "Literata": "literata",
    "Merriweather": "merriweather",
    "Inter": "inter",
    "AtkinsonHyperlegibleNext": "atkinson",
}
FAMILIES = {
    "NotoSansExtended": "sans",
    "NotoSerifExtended": "serif",
    "NotoSansCJKJP": "cjk",
    **{font: slug for slug, (font, _desc, _assets) in SCRIPT_FONTS.items()},
    **CROSSINK_FAMILIES,
}
INDIC_RANGES = {
    "devanagari": "(0x0900-0x097F)",
    "bengali": "(0x0980-0x09FF)",
    "gurmukhi": "(0x0A00-0x0A7F)",
    "gujarati": "(0x0A80-0x0AFF)",
    "oriya": "(0x0B00-0x0B7F)",
    "tamil": "(0x0B80-0x0BFF)",
    "telugu": "(0x0C00-0x0C7F)",
    "kannada": "(0x0C80-0x0CFF)",
    "malayalam": "(0x0D00-0x0D7F)",
    "sinhala": "(0x0D80-0x0DFF)",
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
        crossink = workspace / "crossink"
        run("git", "init", "-q", str(crossink))
        run("git", "-C", str(crossink), "remote", "add", "origin", CROSSINK_URL)
        run("git", "-C", str(crossink), "fetch", "--quiet", "--depth", "1",
            "origin", CROSSINK_REF)
        run("git", "-C", str(crossink), "checkout", "--quiet", "--detach", "FETCH_HEAD")
        ink_catalog = yaml.safe_load((
            crossink/"lib/EpdFont/scripts/sd-fonts.yaml").read_text())
        ink_families = {item["name"]:item for item in ink_catalog["families"]}
        for name in CROSSINK_FAMILIES:
            if name not in ink_families:
                raise RuntimeError("CrossInk font missing from manifest: "+name)
            catalog[name] = ink_families[name]
        # Some script families appear in CrossPoint font *releases*, but are
        # absent from the particular pinned YAML snapshot. For those, use
        # Ubuntu 24.04's system Noto font sources on the CI host. The original
        # CrossPoint converter still produces all CPFONT v4 glyph/metrics and
        # joins the Latin fallback, exactly as it does for upstream families.
        # Never silently omit those 10/11pt fallbacks or use 12pt bitmaps.
        missing = sorted(set(FAMILIES) - set(catalog))
        for name in missing:
            if name == "NotoSansCJKJP":
                root = pathlib.Path("/usr/share/fonts/opentype/noto")
                regular_collection = root / "NotoSansCJK-Regular.ttc"
                # The pinned font converter expects TTF/OTF, not a TTC.
                # Extract the JP member (index 0) from Ubuntu's Noto
                # collection as a CI-host-only TTF, then rasterize it.
                from fontTools.ttLib import TTCollection
                extracted = workspace / "cjk-jp-regular.ttf"
                collection = TTCollection(str(regular_collection))
                try:
                    collection.fonts[0].save(str(extracted))
                finally:
                    collection.close()
                regular = extracted
                bold = pathlib.Path("/nonexistent")
                intervals = "cjk,latin-ext,punctuation,reading,symbols"
            else:
                root = pathlib.Path("/usr/share/fonts/truetype/noto")
                regular = root / f"{name}-Regular.ttf"
                bold = root / f"{name}-Bold.ttf"
                slug = FAMILIES[name]
                intervals = (INDIC_RANGES[slug] if slug in INDIC_RANGES
                             else slug)+",latin-ext,punctuation"
            if not regular.is_file():
                raise RuntimeError(f"Missing font source for {name}: {regular}")
            styles = {"regular": {"path": str(regular)}}
            if bold.is_file():
                styles["bold"] = {"path": str(bold)}
            catalog[name] = {
                "name": name,
                "description": f"Q2 CrossPoint source for {name}",
                "intervals": intervals,
                "styles": styles,
            }
        families = []
        for name in FAMILIES:
            source = dict(catalog[name])
            source["sizes"] = list(
                (10, 11, 12) if name in CROSSINK_FAMILIES else SIZES)
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
            for size in ((10, 11, 12) if name in CROSSINK_FAMILIES else SIZES):
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
        assert len(checksums) == (len(FAMILIES) - len(CROSSINK_FAMILIES))*len(SIZES) + len(CROSSINK_FAMILIES)*3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
