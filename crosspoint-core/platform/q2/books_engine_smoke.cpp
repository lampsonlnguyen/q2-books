// SPDX-License-Identifier: MIT
// Run native, or via qemu-mipsel. Test CrossPoint's actual text/layout primitives;
// this intentionally does NOT substitute for the full EPUB page renderer.
#include <BidiUtils.h>
#include <CjkBreaks.h>
#include <CssParser.h>
#include <EpdFontFamily.h>
#include <WordStore.h>
#include <Utf8.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
int main(int argc, char** argv) {
  if (argc != 1) {
    std::fprintf(stderr, "usage: books-engine-smoke\n");
    return 2;
  }
  WordStore words;
  WordStore::StoredWord word{};
  const char* latin="Nguyễn";
  if (!words.append(latin, std::strlen(latin), word) ||
      words.view(word) != latin) return 1;
  words.release(word);
  if (!BidiUtils::startsWithRtl("مرحبا") ||
      !BidiUtils::startsWithRtl("שלום") ||
      BidiUtils::startsWithRtl("Hello")) return 1;
  std::string visual;
  if (!BidiUtils::applyBidiVisual("مرحبا", visual) || visual.empty()) return 1;
  if (!CjkBreaks::boundaryAllowsBreak(0x4e2d, 0x6587) ||
      CjkBreaks::boundaryAllowsBreak(0x4e2d, 0x3002)) return 1;
  const auto css=CssParser::parseInlineStyle(
      "text-align: center; font-style: italic; font-weight: bold");
  if (css.textAlign != CssTextAlign::Center ||
      css.fontStyle != CssFontStyle::Italic ||
      css.fontWeight != CssFontWeight::Bold) return 1;
  const EpdGlyph glyph{8,9,fp4::fromPixel(9),0,9,0,0};
  const EpdUnicodeInterval interval{0x4e2d,0x4e2d,0};
  EpdFontData data{};
  data.glyph=&glyph;
  data.intervals=&interval;
  data.intervalCount=1;
  EpdFont font(&data);
  if (!font.hasCodepoint(0x4e2d) || font.hasCodepoint(0x4e2e)) return 1;
  std::puts("Books upstream text engine (UTF8, CSS, CJK, BiDi, fonts): OK");
  return 0;
}
