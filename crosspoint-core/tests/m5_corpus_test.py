#!/usr/bin/env python3
"""M5 difficult-EPUB corpus: the reader must open each book, show its text
and styles, and fail cleanly (no crash, no hang) on the malformed one.

Usage: m5_corpus_test.py <books-reader> <corpus-directory>
The corpus is written by make_m5_corpus.py into the same directory.
"""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

reader, corpus = sys.argv[1:]
corpus = Path(corpus)
OK = "ok"
CLEAN_FAIL = "clean-fail"

# book -> (outcome, {chapter: {marker: [required substrings on its line]}})
EXPECT = {
    "nested-css": (OK, {"OEBPS/chapter1.xhtml": {
        "M5-NESTED-INDENT": ["indent=54"],     # two-level descendant selector
        "M5-NESTED-EMPH": ["align=2", "italic=1"],  # inherited centring, em via CSS
        "M5-NESTED-AFTER": []}}),
    "tables": (OK, {"OEBPS/chapter1.xhtml": {m: [] for m in [
        "M5-TABLE-HEAD-A", "M5-TABLE-HEAD-B", "M5-TABLE-CELL-1", "M5-TABLE-CELL-2",
        "M5-TABLE-SPAN-TOP", "M5-TABLE-SPAN-LEFT", "M5-TABLE-SPAN-R1", "M5-TABLE-SPAN-R2",
        "M5-TABLE-OUTER", "M5-TABLE-INNER", "M5-TABLE-AFTER"]}}),
    "ruby": (OK, {"OEBPS/chapter1.xhtml": {m: [] for m in [
        "M5-RUBY-KAN", "M5-RUBY-JP-TEXT", "M5-RUBY-PLAIN"]}}),
    "rtl": (OK, {"OEBPS/chapter1.xhtml": {
        "M5-RTL-ARABIC": ["rtl=1"], "M5-RTL-HEBREW": ["rtl=1"], "M5-RTL-LTR-MIX": ["rtl=1"]}}),
    "footnotes": (OK, {
        "OEBPS/chapter1.xhtml": {m: [] for m in ["M5-NOTE-BODY", "M5-NOTE-TEXT", "M5-NOTE-CROSS"]},
        "OEBPS/chapter2.xhtml": {m: [] for m in ["M5-NOTE-CROSS-TEXT", "M5-NOTE-CH2"]}}),
    "anchors": (OK, {
        "OEBPS/chapter1.xhtml": {m: [] for m in [
            "M5-ANCHOR-LINK", "M5-ANCHOR-HERE", "M5-ANCHOR-SELF", "M5-ANCHOR-BROKEN"]},
        "OEBPS/chapter2.xhtml": {"M5-ANCHOR-TARGET": []}}),
    "images": (OK, {"OEBPS/chapter1.xhtml": {"M5-IMAGE-AFTER": []}}),
    "embedded-font": (OK, {"OEBPS/chapter1.xhtml": {"M5-FONT-EMBEDDED": []}}),
    "empty-chapter": (OK, {"OEBPS/chapter3.xhtml": {"M5-EMPTY-AFTER": []}}),
    "broken-missing-spine": (OK, {"OEBPS/chapter1.xhtml": {"M5-BROKEN-SPINE-OK": []}}),
    # Stray entities and bare ampersands must not cost the chapter.
    "entities": (OK, {"OEBPS/chapter1.xhtml": {
        "M5-ENTITY-NBSP": [], "M5-ENTITY-COPY": ["©"],
        "M5-ENTITY-BARE": ["Tom & Jerry"], "M5-ENTITY-UNKNOWN": ["&undefined;"],
        "M5-ENTITY-END": ["α"]}}),
    # Unclosed and mismatched tags are a known limit: fail cleanly.
    "broken-xml": (CLEAN_FAIL, {}),
}


def run(args, env, timeout=60):
    return subprocess.run([reader, *args], capture_output=True, text=True,
                          timeout=timeout, env=env)


def main():
    books = sorted(corpus.glob("*.epub"))
    names = {b.stem for b in books}
    missing = set(EXPECT) - names
    assert not missing, f"corpus is missing: {sorted(missing)}"
    checked = 0
    with tempfile.TemporaryDirectory(prefix="books-m5-") as tmp:
        env = os.environ.copy()
        env["BOOKS_STATE_DIR"] = tmp
        for name, (outcome, chapters) in EXPECT.items():
            book = str(corpus / f"{name}.epub")
            result = run(["--validate", book], env)
            if outcome == CLEAN_FAIL:
                assert 0 < result.returncode < 128, (name, result.returncode, result.stderr)
                assert "not supported" in result.stderr or "parse error" in result.stderr, (name, result.stderr)
                checked += 1
                print(f"  {name}: fails cleanly (exit {result.returncode})")
                continue
            assert result.returncode == 0, (name, result.returncode, result.stderr)
            walk = run(["--walk", book], env)
            assert walk.returncode == 0, (name, "walk", walk.returncode, walk.stderr)
            for chapter, markers in chapters.items():
                styles = run(["--inspect-styles", book, chapter], env)
                assert styles.returncode == 0, (name, chapter, styles.returncode, styles.stderr)
                lines = styles.stdout.splitlines()
                for marker, required in markers.items():
                    line = next((l for l in lines if marker in l), None)
                    assert line is not None, (name, chapter, marker, styles.stdout)
                    for needle in required:
                        assert needle in line, (name, marker, needle, line)
                    checked += 1
            print(f"  {name}: {sum(len(m) for m in chapters.values())} markers OK")
    print(f"M5 corpus: {checked} checks passed across {len(EXPECT)} books")


if __name__ == "__main__":
    main()
