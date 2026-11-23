#!/usr/bin/env python3
"""Generate the M5 difficult-EPUB reference corpus.

Synthetic books carry unique ASCII markers, and public-domain excerpts use
their own text as markers. The runner checks what reached the reader's text
output, beyond whether the file opened. Broken books are
deliberately malformed; the runner checks that they fail cleanly or degrade
without crashing.

Usage: make_m5_corpus.py <output-directory>
"""
import struct
import sys
import zlib
from pathlib import Path
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile

CONTAINER = """<?xml version="1.0" encoding="UTF-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""


def png_bytes(width=8, height=8):
    """Minimal grey PNG built with the stdlib, enough for an image-block test."""
    raw = b"".join(b"\x00" + bytes([(x * 30 + y * 10) & 255 for x in range(width)])
                   for y in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b""))


def xhtml(title, body, css_href=None, lang="en", dir_attr=None):
    link = f'<link rel="stylesheet" type="text/css" href="{css_href}"/>' if css_href else ""
    dir_part = f' dir="{dir_attr}"' if dir_attr else ""
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops" xml:lang="{lang}" lang="{lang}"{dir_part}>
<head><title>{title}</title>{link}</head>
<body>
{body}
</body>
</html>
"""


def opf(book_id, title, items, spine, extra_metadata="", lang="en"):
    manifest = "\n".join(
        f'<item id="{item_id}" href="{href}" media-type="{mt}"{props}/>'
        for item_id, href, mt, props in items)
    refs = "\n".join(f'<itemref idref="{ref}"/>' for ref in spine)
    return f"""<?xml version="1.0" encoding="UTF-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="id">books-m5-{book_id}</dc:identifier>
<dc:title>{title}</dc:title>
<dc:language>{lang}</dc:language>
{extra_metadata}
</metadata>
<manifest>
{manifest}
</manifest>
<spine>
{refs}
</spine>
</package>
"""


def write_book(path, files):
    """Write a valid EPUB: mimetype first and stored, then the given files."""
    path.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(path, "w") as zf:
        zf.writestr(_info("mimetype", ZIP_STORED), "application/epub+zip")
        zf.writestr(_info("META-INF/container.xml", ZIP_DEFLATED), CONTAINER)
        for name, data in files.items():
            if isinstance(data, str):
                data = data.encode("utf-8")
            zf.writestr(_info(name, ZIP_DEFLATED), data)


def _info(name, compress):
    from zipfile import ZipInfo
    info = ZipInfo(name, date_time=(2026, 1, 1, 0, 0, 0))
    info.compress_type = compress
    return info


def book(name, title, chapters, css=None, extra_files=None, metadata="", spine_order=None, lang="en"):
    """chapters: list of (id, filename, title, body_html, css_href_or_None, dir_attr)."""
    items = [("nav", "nav.xhtml", "application/xhtml+xml", ' properties="nav"')]
    spine = []
    files = {}
    for cid, fname, ctitle, body, css_href, dir_attr in chapters:
        items.append((cid, fname, "application/xhtml+xml", ""))
        spine.append(cid)
        files["OEBPS/" + fname] = xhtml(ctitle, body, css_href, lang=lang, dir_attr=dir_attr)
    if css:
        items.append(("style", "style.css", "text/css", ""))
        files["OEBPS/style.css"] = css
    for fname, data, mt in (extra_files or []):
        items.append((fname.replace(".", "_").replace("/", "_"), fname, mt, ""))
        files["OEBPS/" + fname] = data
    if spine_order is not None:
        spine = spine_order
    files["OEBPS/nav.xhtml"] = xhtml(
        title, '<nav epub:type="toc"><ol>' + "".join(
            f'<li><a href="{f}">{t}</a></li>' for _, f, t, _, _, _ in chapters) + "</ol></nav>", lang=lang)
    files["OEBPS/content.opf"] = opf(name, title, items, spine, metadata, lang=lang)
    return files


def build(out):
    out = Path(out)
    png = png_bytes()

    # 1. Nested CSS: descendant and class selectors, inherited alignment,
    #    display:none, !important, letter-spacing and text-indent.
    css = """
body { font-family: serif; line-height: 1.4; }
.outer .inner p { text-indent: 2em; }
.centred { text-align: center; }
.hidden-note { display: none; }
p.emph { font-style: italic; letter-spacing: 0.05em; }
h1 { font-weight: bold !important; text-align: center; }
"""
    body = """<h1>Nested styles</h1>
<div class="outer"><div class="inner"><p>M5-NESTED-INDENT inside two wrappers.</p></div></div>
<div class="centred"><p class="emph">M5-NESTED-EMPH centred italic paragraph.</p></div>
<p class="hidden-note">M5-NESTED-HIDDEN must not appear.</p>
<p>M5-NESTED-AFTER visible after the hidden block.</p>"""
    files = book("nested-css", "Nested CSS", [
        ("c1", "chapter1.xhtml", "Nested", body, "style.css", None)], css=css)
    write_book(out / "nested-css.epub", files)

    # 2. Tables: simple, colspan/rowspan and a nested table.
    body = """<h1>Tables</h1>
<table><tr><th>M5-TABLE-HEAD-A</th><th>M5-TABLE-HEAD-B</th></tr>
<tr><td>M5-TABLE-CELL-1</td><td>M5-TABLE-CELL-2</td></tr></table>
<table><tr><td colspan="2">M5-TABLE-SPAN-TOP</td></tr>
<tr><td rowspan="2">M5-TABLE-SPAN-LEFT</td><td>M5-TABLE-SPAN-R1</td></tr>
<tr><td>M5-TABLE-SPAN-R2</td></tr></table>
<table><tr><td>M5-TABLE-OUTER<table><tr><td>M5-TABLE-INNER</td></tr></table></td></tr></table>
<p>M5-TABLE-AFTER paragraph after tables.</p>"""
    write_book(out / "tables.epub", book("tables", "Tables", [
        ("c1", "chapter1.xhtml", "Tables", body, None, None)]))

    # 3. Ruby annotations (Japanese) and a CJK paragraph.
    body = """<h1>Ruby</h1>
<p><ruby>漢<rt>M5-RUBY-KAN</rt></ruby><ruby>字<rt>ji</rt></ruby> M5-RUBY-JP-TEXT 読書</p>
<p>M5-RUBY-PLAIN 日本語の段落です。</p>"""
    write_book(out / "ruby.epub", book("ruby", "Ruby", [
        ("c1", "chapter1.xhtml", "Ruby", body, None, None)]))

    # 4. Right-to-left Arabic and Hebrew with embedded LTR numbers.
    body = """<h1>RTL</h1>
<p dir="rtl">M5-RTL-ARABIC مرحبا بالعالم 2026</p>
<p dir="rtl">M5-RTL-HEBREW שלום עולם 42</p>
<p>M5-RTL-LTR-MIX English text with <span dir="rtl">عربي</span> inline.</p>"""
    write_book(out / "rtl.epub", book("rtl", "RTL", [
        ("c1", "chapter1.xhtml", "RTL", body, None, "rtl")]))

    # 5. Footnotes: epub:type noteref in the same file and in another chapter.
    body1 = """<h1>Notes</h1>
<p>M5-NOTE-BODY sentence with a note<a epub:type="noteref" href="#n1">1</a>.</p>
<aside epub:type="footnote" id="n1"><p>M5-NOTE-TEXT the footnote body.</p></aside>
<p>M5-NOTE-CROSS see a note in the next chapter<a epub:type="noteref" href="chapter2.xhtml#n2">2</a>.</p>"""
    body2 = """<h1>More notes</h1>
<aside epub:type="footnote" id="n2"><p>M5-NOTE-CROSS-TEXT from chapter two.</p></aside>
<p>M5-NOTE-CH2 end of chapter two.</p>"""
    write_book(out / "footnotes.epub", book("footnotes", "Footnotes", [
        ("c1", "chapter1.xhtml", "Notes", body1, None, None),
        ("c2", "chapter2.xhtml", "More", body2, None, None)]))

    # 6. Anchors: links into another chapter, to an id in the same chapter,
    #    and one link to an id that does not exist (must not crash).
    body1 = """<h1>Anchors</h1>
<p><a href="chapter2.xhtml#target">M5-ANCHOR-LINK to target</a></p>
<p id="here">M5-ANCHOR-HERE target in this chapter.</p>
<p><a href="#here">M5-ANCHOR-SELF back to here</a> <a href="#missing">M5-ANCHOR-BROKEN</a></p>"""
    body2 = """<h1>Second</h1><p id="target">M5-ANCHOR-TARGET reached by link.</p>"""
    write_book(out / "anchors.epub", book("anchors", "Anchors", [
        ("c1", "chapter1.xhtml", "Anchors", body1, None, None),
        ("c2", "chapter2.xhtml", "Second", body2, None, None)]))

    # 7. Images: PNG, an SVG cover reference, and an image file that is missing.
    body = """<h1>Images</h1>
<p><img src="pic.png" alt="M5-IMAGE-ALT picture"/></p>
<p><img src="missing.png" alt="M5-IMAGE-MISSING alt"/></p>
<p>M5-IMAGE-AFTER text after the images.</p>"""
    write_book(out / "images.epub", book("images", "Images", [
        ("c1", "chapter1.xhtml", "Images", body, None, None)],
        extra_files=[("pic.png", png, "image/png")]))

    # 8. Embedded font declared with @font-face. The file is not a real font,
    #    so the reader must fall back without crashing.
    css = """@font-face { font-family: "M5Face"; src: url(fonts/face.ttf); }
body { font-family: "M5Face", serif; }
p { font-variant: small-caps; }
"""
    body = """<h1>Fonts</h1><p>M5-FONT-EMBEDDED paragraph with a declared face.</p>"""
    write_book(out / "embedded-font.epub", book("embedded-font", "Embedded font", [
        ("c1", "chapter1.xhtml", "Fonts", body, "style.css", None)],
        css=css, extra_files=[("fonts/face.ttf", b"not a real font file", "font/ttf")]))

    # 9. Broken: spine refers to an item missing from the manifest.
    body = """<h1>Broken spine</h1><p>M5-BROKEN-SPINE-OK first chapter.</p>"""
    files = book("broken-spine", "Broken spine", [
        ("c1", "chapter1.xhtml", "One", body, None, None)],
        spine_order=["c1", "c-missing"])
    write_book(out / "broken-missing-spine.epub", files)

    # 10. Stray entities: &nbsp;, &copy;, a bare & and an unknown name in
    #     otherwise well-formed XHTML. Must open and show every marker.
    body = """<h1>Entities</h1>
<p>M5-ENTITY-NBSP a&nbsp;b M5-ENTITY-COPY &copy; 2026</p>
<p>M5-ENTITY-BARE Tom & Jerry &amp; friends</p>
<p>M5-ENTITY-UNKNOWN &undefined; stays visible</p>
<p>M5-ENTITY-END &#160; &#x3B1; numeric references.</p>"""
    write_book(out / "entities.epub", book("entities", "Entities", [
        ("c1", "chapter1.xhtml", "Entities", body, None, None)]))

    # 11. Broken: the first paragraph and its inline formatting are unclosed.
    bad = """<?xml version="1.0" encoding="UTF-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Bad</title></head>
<body><h1>Malformed</h1><p>M5-BROKEN-XML-OK survives <b><em>unclosed formatting
<p>M5-BROKEN-XML-PLAIN second paragraph</p></body></html>
"""
    files = {
        "OEBPS/content.opf": opf("broken-xml", "Broken XML", [
            ("nav", "nav.xhtml", "application/xhtml+xml", ' properties="nav"'),
            ("c1", "chapter1.xhtml", "application/xhtml+xml", "")], ["c1"]),
        "OEBPS/chapter1.xhtml": bad,
        "OEBPS/nav.xhtml": xhtml("Broken XML", '<nav epub:type="toc"><ol><li><a href="chapter1.xhtml">Bad</a></li></ol></nav>'),
    }
    write_book(out / "broken-xml.epub", files)
    files["OEBPS/chapter1.xhtml"] = bad.replace(
        "<p>M5-BROKEN-XML-PLAIN", "</em></b></p><p>M5-BROKEN-XML-PLAIN", 1)
    write_book(out / "broken-xml-reference.epub", files)

    # 12. Broken: empty chapter body and a chapter that is only whitespace.
    files = book("empty-chapter", "Empty chapter", [
        ("c1", "chapter1.xhtml", "Empty", "<h1>Empty</h1>", None, None),
        ("c2", "chapter2.xhtml", "Blank", "   \n  ", None, None),
        ("c3", "chapter3.xhtml", "After", "<p>M5-EMPTY-AFTER text after empty chapters.</p>", None, None)])
    write_book(out / "empty-chapter.epub", files)

    # Public-domain table excerpt from The Principles of Chemistry, Volume II
    # (Project Gutenberg #54210), preserving the source's cell spans and breaks.
    body = """<table summary="table_3">
  <tr>
    <td class="td1" rowspan="2" colspan="4">Molecular composition of the<br />higher hydrogen and<br />metallo-organic compounds</td>
    <td class="td1" rowspan="2" colspan="3">Atomic weights of the elements</td>
    <td class="td1" rowspan="2" colspan="9">Composition of the saline compounds, X=Cl</td>
    <td class="td1" rowspan="2">Peroxides</td>
    <td class="td1" rowspan="2">Lower hydrogen<br />compounds</td>
    <td class="td2" colspan="3">Simple bodies</td>
  </tr>
  <tr>
    <td class="td3">Sp. gr.</td>
    <td class="td3">Sp. vol.</td>
    <td class="td4">Melting<br />point</td>
  </tr>
</table>"""
    write_book(out / "chemistry-table.epub", book("chemistry-table", "The Principles of Chemistry", [
        ("c1", "chapter1.xhtml", "Periodic table", body, None, None)]))

    # Public-domain Japanese prose and reading from Aozora Bunko's Rashōmon,
    # card 127. Encode the supplied reading as EPUB ruby.
    body = """<p>その代りまた<ruby>鴉<rp>（</rp><rt>からす</rt><rp>）</rp></ruby>がどこからか、たくさん集って来た。</p>"""
    write_book(out / "rashomon-ruby.epub", book("rashomon-ruby", "羅生門", [
        ("c1", "chapter1.xhtml", "羅生門", body, None, None)], lang="ja"))

    print(f"wrote M5 corpus to {out}")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        sys.exit("usage: make_m5_corpus.py <output-directory>")
    build(sys.argv[1])
