#!/usr/bin/env python3
"""Deterministic, short visual glyph atlas for the CrossPod Q2 EPUB reader.

The 14 additional font scripts correspond to reader/ScriptFonts.hpp.
Latin/Cyrillic/Greek/Thai probes are included for visibility testing, NOT
as a claim that the current device fonts cover every glyph or shaping rule.
Use --check to verify an existing EPUB instead of rewriting it.
"""
from __future__ import annotations

import argparse
import html
from pathlib import Path
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET
from zipfile import ZIP_DEFLATED, ZIP_STORED, ZipFile, ZipInfo

TITLE = "CrossPod — Multilingual Glyph Visibility Test"
EPUB_NAME = "CrossPod-Multilingual-Visibility-Test.epub"
# One short paragraph per language, with both cases where they exist.
# Unicase scripts show the available letters, vowel signs and combinations.
# These are visibility specimens, not exhaustive Unicode repertoire dumps.
SAMPLES = [
    ("English / Latin", "en", "latin", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz 0123456789"),
    ("Vietnamese", "vi", "latin", "A Ă Â B C D Đ E Ê G H I K L M N O Ô Ơ P Q R S T U Ư V X Y · a ă â b c d đ e ê g h i k l m n o ô ơ p q r s t u ư v x y · Á À Ả Ã Ạ Ắ Ằ Ẳ Ẵ Ặ Ấ Ầ Ẩ Ẫ Ậ É È Ẻ Ẽ Ẹ Ế Ề Ể Ễ Ệ Í Ì Ỉ Ĩ Ị Ó Ò Ỏ Õ Ọ Ố Ồ Ổ Ỗ Ộ Ớ Ờ Ở Ỡ Ợ Ú Ù Ủ Ũ Ụ Ứ Ừ Ử Ữ Ự Ý Ỳ Ỷ Ỹ Ỵ · á à ả ã ạ ắ ằ ẳ ẵ ặ ấ ầ ẩ ẫ ậ é è ẻ ẽ ẹ ế ề ể ễ ệ í ì ỉ ĩ ị ó ò ỏ õ ọ ố ồ ổỗ ộ ớ ờ ở ỡ ợ ú ù ủ ũ ụ ứ ừử ữ ự ý ỳ ỷ ỹỵ"),
    ("French", "fr", "latin", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz À Â Æ Ç É È Ê Ë Î Ï Ô Œ Ù Û Ü Ÿ à â æ ç é è ê ë î ï ô œ ù û ü ÿ"),
    ("German", "de", "latin", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz Ä Ö Ü ẞ ä ö ü ß"),
    ("Spanish", "es", "latin", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz Á É Í Ñ Ó Ú Ü á é í ñ ó ú ü ¿ ¡"),
    ("Portuguese", "pt", "latin", "ABCDEFGHIJKLMNOPQRSTUVWXYZ abcdefghijklmnopqrstuvwxyz Á Â Ã À Ç É Ê Í Ó Ô Õ Ú á â ã à ç é ê í ó ô õ ú"),
    ("Turkish", "tr", "latin", "A B C Ç D E F G Ğ H I İ J K L M N O Ö P R S Ş T U Ü V Y Z · a b c ç d e f g ğ h ı i j k l m n o ö p r s ş t u ü v y z"),
    ("Greek", "el", "greek", "Α Β Γ Δ Ε Ζ Η Θ Ι Κ Λ Μ Ν Ξ Ο Π Ρ Σ Τ Υ Φ Χ Ψ Ω · α β γ δ ε ζ η θ ι κ λ μ ν ξ ο π ρ σ ς τ υ φ χ ψ ω ά έ ή ί ό ύ ώ ϊ ϋ ΐ ΰ"),
    ("Russian / Cyrillic", "ru", "cyrillic", "А Б В Г Д Е Ё Ж З И Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ъ Ы Ь Э Ю Я · а б в г д е ё ж з и й к л м н о п р с т у ф х ц ч ш щ ъ ы ь э ю я"),
    ("Ukrainian / Cyrillic", "uk", "cyrillic", "А Б В Г Ґ Д Е Є Ж З И І Ї Й К Л М Н О П Р С Т У Ф Х Ц Ч Ш Щ Ь Ю Я · а б в г ґ д е є ж з и і ї й к л м н о п р с т у ф х ц ч ш щ ь ю я"),
    ("Chinese Simplified", "zh-Hans", "cjk", "一 二 三 四 五 六 七 八 九 十 中 文 简 体 汉 字 测 试 书 本 页 面 欢 迎 阅 读 世 界"),
    ("Chinese Traditional", "zh-Hant", "cjk", "一 二 三 四 五 六 七 八 九 十 中 文 繁 體 漢 字 測 試 書 本 頁 面 歡 迎 閱 讀 世 界"),
    ("Japanese / Hiragana Katakana Kanji", "ja", "cjk", "あ い う え お か き く け こ さ し す せ そ た ち つ て と な に ぬ ね の は ひ ふ へ ほ ま み む め も や ゆ よ ら り る れ ろ わ を ん · ア イ ウ エ オ カ キ ク ケ コ サ シ ス セ ソ タ チ ツ テ ト ナ ニ ヌ ネ ノ ハ ヒ フ ヘ ホ マ ミ ム メ モ ヤ ユ ヨ ラ リ ル レ ロ ワ ヲ ン · 日 本 語 漢 字"),
    ("Korean / Hangul", "ko", "korean", "가 나 다 라 마 바 사 아 자 차 카 타 파 하 · 각 간 갈 감 강 개 거 고 구 기 김 나 너 누 니 다 도 두 라 로 루 마 모 무 바 보 부 서 소 수 아 어 오 우 이 자 저 조 주 지 차 처 초 추 카 타 파 하 · ᄀ ᄂ ᄃ ᄅ ᄆ ᄇ ᄉ ᄋ ᄌ ᄎ ᄏ ᄐ ᄑ ᄒ"),
    ("Hindi / Devanagari", "hi", "devanagari", "अ आ इ ई उ ऊ ऋ ए ऐ ओ औ अं अः · क ख ग घ ङ च छ ज झ ञ ट ठ ड ढ ण त थ द ध न प फ ब भ म य र ल व श ष स ह · ा ि ी ु ू े ै ो ौ ् · क्ष त्र ज्ञ नमस्ते"),
    ("Marathi / Devanagari", "mr", "devanagari", "अ आ इ ई उ ऊ ए ऐ ओ औ अं · क ख ग घ च छ ज झ ट ठ ड ढ ण त थ द ध न प फ ब भ म य र ल व श ष स ह ळ · मराठी वाचन"),
    ("Nepali / Devanagari", "ne", "devanagari", "अ आ इ ई उ ऊ ऋ ए ऐ ओ औ · क ख ग घ ङ च छ ज झ ञ ट ठ ड ढ ण त थ द ध न प फ ब भ म य र ल व श ष स ह · नेपाली अक्षर"),
    ("Bengali", "bn", "bengali", "অ আ ই ঈ উ ঊ ঋ এ ঐ ও ঔ · ক খ গ ঘ ঙ চ ছ জ ঝ ঞ ট ঠ ড ঢ ণ ত থ দ ধ ন প ফ ব ভ ম য র ল শ ষ স হ ড় ঢ় য় · া ি ী ু ূ ে ৈ ো ৌ ্ · বাংলা"),
    ("Assamese", "as", "bengali", "অ আ ই ঈ উ ঊ ঋ এ ঐ ও ঔ · ক খ গ ঘ ঙ চ ছ জ ঝ ঞ ট ঠ ড ঢ ণ ত থ দ ধ ন প ফ ব ভ ম য ৰ ল ৱ শ ষ স হ · অসমীয়া"),
    ("Gujarati", "gu", "gujarati", "અ આ ઇ ઈ ઉ ઊ ઋ એ ઐ ઓ ઔ · ક ખ ગ ઘ ઙ ચ છ જ ઝ ઞ ટ ઠ ડ ઢ ણ ત થ દ ધ ન પ ફ બ ભ મ ય ર લ વ શ ષ સ હ ળ · ા િ ી ુ ૂ ે ૈ ો ૌ ્"),
    ("Punjabi / Gurmukhi", "pa", "gurmukhi", "ੳ ਅ ੲ ਸ ਹ ਕ ਖ ਗ ਘ ਙ ਚ ਛ ਜ ਝ ਞ ਟ ਠ ਡ ਢ ਣ ਤ ਥ ਦ ਧ ਨ ਪ ਫ ਬ ਭ ਮ ਯ ਰ ਲ ਵ ੜ · ਾ ਿ ੀ ੁ ੂ ੇ ੈ ੋ ੌ ੍ · ਪੰਜਾਬੀ"),
    ("Kannada", "kn", "kannada", "ಅ ಆ ಇ ಈ ಉ ಊ ಋ ಎ ಏ ಐ ಒ ಓ ಔ · ಕ ಖ ಗ ಘ ಙ ಚ ಛ ಜ ಝ ಞ ಟ ಠ ಡ ಢ ಣ ತ ಥ ದ ಧ ನ ಪ ಫ ಬ ಭ ಮ ಯ ರ ಲ ವ ಶ ಷ ಸ ಹ ಳ · ಾ ಿ ೀ ು ೂ ೇ ೈ ೋ ೌ ್"),
    ("Malayalam", "ml", "malayalam", "അ ആ ഇ ഈ ഉ ഊ ഋ എ ഏ ഐ ഒ ഓ ഔ · ക ഖ ഗ ഘ ങ ച ഛ ജ ഝ ഞ ട ഠ ഡ ഢ ണ ത ഥ ദ ധ ന പ ഫ ബ ഭ മ യ ര ല വ ശ ഷ സ ഹ ള ഴ റ · ാ ി ീ ു ൂ െ േ ൈ ൊ ോ ൌ ്"),
    ("Odia / Oriya", "or", "oriya", "ଅ ଆ ଇ ଈ ଉ ଊ ଋ ଏ ଐ ଓ ଔ · କ ଖ ଗ ଘ ଙ ଚ ଛ ଜ ଝ ଞ ଟ ଠ ଡ ଢ ଣ ତ ଥ ଦ ଧ ନ ପ ଫ ବ ଭ ମ ଯ ର ଲ ଵ ଶ ଷ ସ ହ ଳ · ା ି ୀ ୁ ୂ େ ୈ ୋ ୌ ୍"),
    ("Sinhala", "si", "sinhala", "අ ආ ඇ ඈ ඉ ඊ උ ඌ එ ඒ ඓ ඔ ඕ ඖ · ක ඛ ග ඝ ඞ ච ඡ ජ ඣ ඤ ට ඨ ඩ ඪ ණ ත ථ ද ධ න ප ඵ බ භ ම ය ර ල ව ශ ෂ ස හ ළ ෆ · ා ැ ෑ ි ී ු ූ ෙ ේ ෛ ො ෝ ෞ ්"),
    ("Tamil", "ta", "tamil", "அ ஆ இ ஈ உ ஊ எ ஏ ஐ ஒ ஓ ஔ · க ங ச ஞ ட ண த ந ப ம ய ர ல வ ழ ள ற ன ஜ ஷ ஸ ஹ · ா ி ீ ு ூ ெ ே ை ொ ோ ௌ ் · தமிழ்"),
    ("Telugu", "te", "telugu", "అ ఆ ఇ ఈ ఉ ఊ ఋ ఎ ఏ ఐ ఒ ఓ ఔ · క ఖ గ ఘ ఙ చ ఛ జ ఝ ఞ ట ఠ డ ఢ ణ త థ ద ధ న ప ఫ బ భ మ య ర ల వ శ ష స హ ళ · ా ి ీ ు ూ ె ే ై ొ ో ౌ ్"),
    ("Arabic (right to left)", "ar", "arabic", "ا ب ت ث ج ح خ د ذ ر ز س ش ص ض ط ظ ع غ ف ق ك ل م ن ه و ي ء أ إ آ ة ى · َ ِ ُ ْ ّ ً ٍ ٌ · العربية مرحبًا", "rtl"),
    ("Hebrew (right to left)", "he", "hebrew", "א ב ג ד ה ו ז ח ט י כ ך ל מ ם נ ן ס ע פ ף צ ץ ק ר ש ת · ְ ֱ ֲ ֳ ִ ֵ ֶ ַ ָ ֹ ֻ ּ · עברית שלום", "rtl"),
    ("Thai (additional coverage probe)", "th", "thai", "ก ข ฃ ค ฅ ฆ ง จ ฉ ช ซ ฌ ญ ฎ ฏ ฐ ฑ ฒ ณ ด ต ถ ท ธ น บ ป ผ ฝ พ ฟ ภ ม ย ร ล ว ศ ษ ส ห ฬ อ ฮ · ะ า ิ ี ึ ื ุ ู เ แ โ ใ ไ ่ ้ ๊ ๋ ์"),
]
# The current reader explicitly installs only these script fallbacks.
FALLBACK_SCRIPTS = {
    "cjk", "korean", "devanagari", "bengali", "gujarati", "gurmukhi",
    "kannada", "malayalam", "oriya", "sinhala", "tamil", "telugu",
    "arabic", "hebrew",
}
CONTAINER = """<?xml version="1.0" encoding="utf-8"?>
<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0">
<rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
def build_chapter() -> str:
    paragraphs = [
        '<h1>CrossPod Multilingual Visibility Test</h1>',
        '<p>This is a visual font check, not a guarantee of correct shaping or coverage. '
        'A missing letter may appear as a blank space, square, or replacement glyph. '
        'Scripts without letter case are shown once.</p>',
    ]
    for name, lang, script, chars, *direction in SAMPLES:
        for char in chars:
            if unicodedata.category(char) == "Cs":
                raise ValueError("Surrogate character")
        rtl = ' dir="rtl"' if direction and direction[0] == "rtl" else ""
        escaped = html.escape(chars)
        badge = "fallback" if script in FALLBACK_SCRIPTS else "probe"
        paragraphs.append(
            f'<p xml:lang="{lang}"{rtl}><strong>{html.escape(name)} '
            f'[{badge}]:</strong> {escaped}</p>'
        )
    return ('<?xml version="1.0" encoding="utf-8"?>\n'
            '<html xmlns="http://www.w3.org/1999/xhtml" '
            'xml:lang="en"><head><title>Language visibility</title></head>'
            '<body>' + "".join(paragraphs) + '</body></html>')
def make_epub(dest: Path) -> None:
    chapter = build_chapter()
    opf = """<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0"
 unique-identifier="bookid">
<metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
<dc:identifier id="bookid">urn:crosspod:multilingual-visibility:1</dc:identifier>
<dc:title>CrossPod Multilingual Visibility Test</dc:title>
<dc:creator>CrossPod contributors</dc:creator>
<dc:language>mul</dc:language></metadata>
<manifest><item id="chapter" href="chapter.xhtml"
 media-type="application/xhtml+xml"/></manifest>
<spine><itemref idref="chapter"/></spine>
</package>"""
    entries = [
        ("mimetype", b"application/epub+zip", ZIP_STORED),
        ("META-INF/container.xml", CONTAINER.encode(), ZIP_DEFLATED),
        ("OEBPS/content.opf", opf.encode(), ZIP_DEFLATED),
        ("OEBPS/chapter.xhtml", chapter.encode(), ZIP_DEFLATED),
    ]
    dest.parent.mkdir(parents=True, exist_ok=True)
    with ZipFile(dest, "w") as out:
        for name, data, compression in entries:
            info = ZipInfo(name, date_time=(2020, 1, 1, 0, 0, 0))
            info.compress_type = compression
            info.external_attr = 0o644 << 16
            out.writestr(info, data)
def check_epub(path: Path) -> None:
    with ZipFile(path) as book:
        names = book.namelist()
        assert names == ["mimetype", "META-INF/container.xml",
                         "OEBPS/content.opf", "OEBPS/chapter.xhtml"], names
        assert book.getinfo("mimetype").compress_type == ZIP_STORED
        assert book.read("mimetype") == b"application/epub+zip"
        assert book.testzip() is None
        xhtml = ET.fromstring(book.read("OEBPS/chapter.xhtml"))
        ET.fromstring(book.read("OEBPS/content.opf"))
        ET.fromstring(book.read("META-INF/container.xml"))
        paragraphs = xhtml.findall("{http://www.w3.org/1999/xhtml}body/"
                                   "{http://www.w3.org/1999/xhtml}p")
        assert len(paragraphs) == len(SAMPLES) + 1
        for entry, node in zip(SAMPLES, paragraphs[1:]):
            assert node.attrib.get("{http://www.w3.org/XML/1998/namespace}lang") == entry[1]
            assert entry[3] in "".join(node.itertext()), entry[0]
        assert {entry[2] for entry in SAMPLES if entry[2] in FALLBACK_SCRIPTS} == FALLBACK_SCRIPTS
        assert book.read("OEBPS/chapter.xhtml").decode() == build_chapter()
def main() -> int:
    p = argparse.ArgumentParser()
    p.add_argument("path", type=Path)
    p.add_argument("--check", action="store_true",
                   help="verify a previously generated EPUB")
    args = p.parse_args()
    if not args.check:
        make_epub(args.path)
    check_epub(args.path)
    print(f"CrossPod multilingual visibility: {len(SAMPLES)} language rows, "
          f"{len(FALLBACK_SCRIPTS)} configured fallback scripts: {args.path}")
    return 0
if __name__ == "__main__":
    sys.exit(main())
