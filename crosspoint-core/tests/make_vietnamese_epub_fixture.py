#!/usr/bin/env python3
"""Generate an original Vietnamese prose EPUB for actual CrossPoint raster QA."""
import sys
from zipfile import ZipFile, ZIP_DEFLATED, ZIP_STORED

container = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="bookid">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="bookid">crosspod-vietnamese-original-demo</dc:identifier>
<dc:title>Sương sớm trên đồi thông</dc:title>
<dc:creator>CrossPod — nguyên tác thử nghiệm</dc:creator>
<dc:language>vi</dc:language></metadata>
<manifest><item id="chapter" href="chapter1.xhtml" media-type="application/xhtml+xml"/>
<item id="css" href="book.css" media-type="text/css"/></manifest>
<spine><itemref idref="chapter"/></spine>
</package>"""
# This is original test prose, not an excerpt from a third-party book.
chapter = """<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="vi"><head>
<title>Sương sớm trên đồi thông</title><link rel="stylesheet" href="book.css"/></head>
<body>
<h1>Sương sớm trên đồi thông</h1>
<p>Đà Lạt thức dậy trong làn sương mỏng. Những ngọn thông đứng im trên triền đồi, còn giọt nước đọng nơi đầu lá lấp lánh dưới ánh bình minh.</p>
<p>Minh kéo cao cổ áo, bước chậm trên con đường đất ẩm. Từ phía thung lũng, tiếng chim gọi nhau vang lên rất khẽ, như sợ đánh thức những mái nhà còn đang ngủ.</p>
<p>Ở cuối lối đi, bà ngoại chờ bên ấm trà nóng. Bà mỉm cười: “Cháu đã thấy mặt trời chưa?” Minh lắc đầu. Nhưng đúng lúc ấy, một vệt nắng vàng xuyên qua màn sương.</p>
<p>Buổi sáng bỗng trở nên dịu dàng. Minh biết rằng mình sẽ nhớ mãi khoảnh khắc bình yên này, dù mai sau có đi đến những chân trời xa xôi.</p>
</body></html>"""
css = "body { margin: 16px; } h1 { text-align: center; } p { text-indent: 1.5em; line-height: 1.5; }"
if len(sys.argv) != 2:
    raise SystemExit("Usage: make_vietnamese_epub_fixture.py <output.epub>")
with ZipFile(sys.argv[1], "w") as archive:
    archive.writestr("mimetype", "application/epub+zip", compress_type=ZIP_STORED)
    archive.writestr("META-INF/container.xml", container, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/content.opf", opf, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/chapter1.xhtml", chapter, compress_type=ZIP_DEFLATED)
    archive.writestr("OEBPS/book.css", css, compress_type=ZIP_DEFLATED)
