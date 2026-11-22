#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <builtinFonts/notosans_12_regular.h>
#include <HalDisplay.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

// Host smoke test links the complete original CrossPoint renderer object,
// so undefined font/shaping/graphics dependencies fail at link time.
int main() {
  HalDisplay display;
  display.begin();
  GfxRenderer gfx(display);
  // Q2PagePresenter consumes a 375x320 logical surface; CrossPoint's default
  // Portrait orientation would rotate once here and a second time on LCD.
  gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  gfx.begin();
  assert(gfx.getScreenWidth() == 375);
  assert(gfx.getScreenHeight() == 320);
  gfx.clearScreen(0xff);
  gfx.drawPixel(12, 13, true);
  auto* pixels=display.getFrameBuffer();
  assert(pixels);
  const unsigned at=13*HalDisplay::DISPLAY_WIDTH_BYTES+12/8;
  const uint8_t mask=static_cast<uint8_t>(0x80u>>(12&7));
  assert((pixels[at] & mask)==0);
  // Real upstream Noto Sans glyph decompression and Vietnamese text rendering.
  // This must link against CrossPoint's original shaping and raster pipeline.
  EpdFont noto(&notosans_12_regular);
  assert(noto.hasCodepoint(0x1EBF));  // ế
  assert(noto.hasCodepoint(0x1EEF));  // ữ
  gfx.insertFont(1, EpdFontFamily(12, &noto));
  // Compressed upstream built-in fonts require the original decompressor;
  // without it, GfxRenderer correctly skips glyphs (rather than drawing text).
  FontDecompressor decompressor;
  assert(decompressor.init());
  FontCacheManager fontCache(gfx.getFontMap(), gfx.getSdCardFonts(), gfx.getTtfFonts());
  fontCache.setFontDecompressor(&decompressor);
  gfx.setFontCacheManager(&fontCache);
  gfx.drawText(1, 20, 60, "Tiếng Việt: chữ ế và ữ");
  unsigned inkPixels = 0;
  for (uint32_t i = 0; i < display.getBufferSize(); ++i) {
    uint8_t byte = pixels[i];
    for (unsigned bit = 0; bit < 8; ++bit) inkPixels += ((byte >> bit) & 1u) == 0;
  }
  assert(inkPixels > 15);  // A one-pixel smoke test cannot satisfy this.

  std::puts("Books upstream GfxRenderer linked and painted into Q2 HAL");
  return 0;
}
