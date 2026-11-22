#!/usr/bin/env python3
"""Real-world markup a reader must take: HTML entities in EPUB3 chapters, and italic/bold
drawn in their own faces with intact word spaces.

Usage: markup_tolerance_test.py READER entities|italic
CROSSPOD_TEST_FONTS: a folder of the card's .cpfont files (tools/fetch_q2_fonts.py). Without
it, the italic check is skipped (exit 77): the built-in emergency font has no italic face."""
import io
import os
import pathlib
import subprocess
import sys
import tempfile
import zipfile

reader, part = sys.argv[1:]
assert part in ('entities', 'italic'), part
SKIP = 77

CONTAINER = (b'<?xml version="1.0"?><container version="1.0" '
             b'xmlns="urn:oasis:names:tc:opendocument:xmlns:container"><rootfiles>'
             b'<rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/>'
             b'</rootfiles></container>')
OPF = (b'<?xml version="1.0" encoding="utf-8"?><package xmlns="http://www.idpf.org/2007/opf" '
       b'version="3.0" unique-identifier="id"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/">'
       b'<dc:identifier id="id">markup</dc:identifier><dc:title>Markup</dc:title><dc:language>en</dc:language>'
       b'</metadata><manifest><item id="c" href="c.xhtml" media-type="application/xhtml+xml"/>'
       b'<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/></manifest>'
       b'<spine><itemref idref="c"/></spine></package>')
NAV = (b'<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml" '
       b'xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>'
       b'<li><a href="c.xhtml">One</a></li></ol></nav></body></html>')


def epub(path, doctype, body):
    chapter = ('<?xml version="1.0" encoding="utf-8"?>\n' + doctype +
               '<html xmlns="http://www.w3.org/1999/xhtml"><head><title>c</title></head>'
               f'<body>{body}</body></html>').encode()
    buf = io.BytesIO()
    with zipfile.ZipFile(buf, 'w', zipfile.ZIP_DEFLATED) as z:
        z.writestr(zipfile.ZipInfo('mimetype'), b'application/epub+zip', compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml', CONTAINER)
        z.writestr('OEBPS/content.opf', OPF)
        z.writestr('OEBPS/nav.xhtml', NAV)
        z.writestr('OEBPS/c.xhtml', chapter)
    path.write_bytes(buf.getvalue())


def render(book, state):
    out = book.with_suffix('.pgm')
    result = subprocess.run([reader, '--render', str(book), str(out)], capture_output=True, text=True,
                            timeout=60, env=dict(os.environ, CROSSPOD_STATE_DIR=str(state)))
    assert result.returncode == 0, (book.name, result.stdout, result.stderr)
    data = out.read_bytes()
    assert data.startswith(b'P5'), data[:16]
    header = data.split(b'\n', 3)
    width, height = map(int, header[1].split())
    return width, height, header[3]


def ink_rows(width, height, pixels):
    return [y for y in range(height) if any(pixels[y * width + x] < 128 for x in range(width))]


with tempfile.TemporaryDirectory(prefix='crosspod-markup-') as tmp:
    tmp = pathlib.Path(tmp)
    # &nbsp;, &mdash;, &hellip;: HTML entities the reader's table expands. Under EPUB3's
    # <!DOCTYPE html> or no DOCTYPE, expat used to fail the chapter and the whole book.
    doctypes = (('html5', '<!DOCTYPE html>\n'), ('none', ''),
                ('xhtml11', '<!DOCTYPE html PUBLIC "-//W3C//DTD XHTML 1.1//EN" '
                            '"http://www.w3.org/TR/xhtml11/DTD/xhtml11.dtd">\n'))
    for name, doctype in doctypes if part == 'entities' else ():
        book = tmp / f'entity-{name}.epub'
        epub(book, doctype, '<p>Tom&nbsp;and&nbsp;Jerry &mdash; again&hellip;</p>')
        state = tmp / f'state-{name}'
        state.mkdir()
        w, h, px = render(book, state)
        assert ink_rows(w, h, px), f'{name}: entity chapter drew nothing'
    if part == 'entities':
        print('HTML entities: EPUB3, no-DOCTYPE and XHTML 1.1 chapters open and draw')
        sys.exit(0)

    fonts = os.environ.get('CROSSPOD_TEST_FONTS')
    if not fonts or not (pathlib.Path(fonts) / 'sans-12.cpfont').exists():
        print('Italic faces: SKIP (set CROSSPOD_TEST_FONTS to the card font folder)')
        sys.exit(SKIP)

    def page(style, text):
        book = tmp / f'style-{style}-{len(text)}.epub'
        # Short and left-aligned: no justification stretches the gaps.
        epub(book, '<!DOCTYPE html>\n', f'<p style="font-style:{style};text-align:left">{text}</p>')
        state = tmp / f'state-style-{style}-{len(text)}'
        state.mkdir()
        (state / 'fonts').symlink_to(pathlib.Path(fonts).resolve())
        return render(book, state)

    def line_band(width, height, pixels):
        rows = ink_rows(width, height, pixels)
        top = rows[0]
        end = top
        while end + 1 < height and end + 1 in rows:
            end += 1
        return top, end

    def gaps(width, height, pixels):
        top, end = line_band(width, height, pixels)
        cols = [x for x in range(width) if any(pixels[y * width + x] < 128 for y in range(top, end + 1))]
        return sorted((b - a - 1 for a, b in zip(cols, cols[1:])), reverse=True)

    # "Before the image": the words that ran together ("Beforethe"). Two word spaces: the
    # second widest gap must still be a space, not a letter gap.
    text = 'Before the image'
    regular, italic = gaps(*page('normal', text)), gaps(*page('italic', text))
    assert italic[1] >= max(3, regular[1] // 2), ('word spaces', regular[:3], italic[:3])

    # One tall stroke: drawn in the italic face it leans right, in the regular face it stands.
    def lean(width, height, pixels):
        top, end = line_band(width, height, pixels)
        def left(y): return min(x for x in range(width) if pixels[y * width + x] < 128)
        rows = [y for y in range(top, end + 1) if any(pixels[y * width + x] < 128 for x in range(width))]
        return left(rows[0]) - left(rows[-1])
    lean_regular, lean_italic = lean(*page('normal', 'l')), lean(*page('italic', 'l'))
    assert lean_italic >= lean_regular + 2, ('italic drew upright', lean_regular, lean_italic)
    print(f'Italic faces: drawn slanted ({lean_italic}px lean), word spaces {italic[1]}px '
          f'(regular {regular[1]}px)')
