#include <EpdFontFamily.h>
#include <cassert>
#include <cstring>
int main() {
  // Synthetic font data uses the real CrossPoint metrics and Unicode lookup
  // functions; it does not assert that the device ships every language font.
  const EpdGlyph glyphs[3] = {
      {8, 10, fp4::fromPixel(9), 0, 10, 8, 0},
      {7, 9, fp4::fromPixel(8), 0, 9, 8, 8},
      {8, 10, fp4::fromPixel(9), 0, 10, 8, 16}
  };
  const EpdUnicodeInterval intervals[] = {
      {0x0041, 0x0041, 0},
      {0x00E9, 0x00E9, 1},
      {0x1EA1, 0x1EA1, 2}
  };
  EpdFontData data{};
  data.glyph = glyphs;
  data.intervals = intervals;
  data.intervalCount = 3;
  data.ascender = 10;
  EpdFont regular(&data);
  assert(regular.hasCodepoint('A'));
  assert(regular.hasCodepoint(0x00E9));
  assert(regular.hasCodepoint(0x1EA1));
  assert(!regular.hasCodepoint(0x4E2D));
  assert(regular.getGlyph('A') == &glyphs[0]);
  assert(regular.getGlyph(0x1EA1) == &glyphs[2]);
  EpdGlyph fallback{};
  assert(regular.getGlyphMetrics(syntheticGlyph::FULL_BLOCK, fallback) == &fallback);
  assert(fallback.width > 0 && fallback.height > 0);
  int width=0,height=0;
  regular.getTextDimensions("A", &width, &height);
  assert(width >= 8 && height >= 10);
  EpdFontFamily family(12, &regular);
  assert(family.hasCodepoint(0x1EA1, EpdFontFamily::REGULAR));
  assert(!family.hasCodepoint(0x4E2D, EpdFontFamily::REGULAR));
  assert(family.getGlyph(0x00E9, EpdFontFamily::REGULAR) == &glyphs[1]);
  return 0;
}
