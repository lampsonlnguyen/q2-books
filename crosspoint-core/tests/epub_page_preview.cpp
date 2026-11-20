// SPDX-License-Identifier: MIT
// End-to-end text-page diagnostic: real EPUB ZIP, original CrossPoint
// ParsedText/TextBlock line layout and original GfxRenderer. XHTML
// extraction in this test is intentionally NOT the production HTML engine.
#include <ParsedText.h>
#include <GfxRenderer.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <HalDisplay.h>
#include <ZipFile.h>
#include <builtinFonts/notosans_12_regular.h>
#include <expat.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

struct Chapter {
  std::vector<std::string> paragraphs;
  std::string current;
  bool capture = false;
};
void XMLCALL onStart(void* ctx, const XML_Char* tag, const XML_Char**) {
  auto& c = *static_cast<Chapter*>(ctx);
  if (std::strcmp(tag, "p") == 0 || std::strcmp(tag, "h1") == 0) {
    c.capture = true;
    c.current.clear();
  }
}
void XMLCALL onText(void* ctx, const XML_Char* chars, int count) {
  auto& c = *static_cast<Chapter*>(ctx);
  if (c.capture && count > 0) c.current.append(chars, static_cast<size_t>(count));
}
void XMLCALL onEnd(void* ctx, const XML_Char* tag) {
  auto& c = *static_cast<Chapter*>(ctx);
  if (c.capture && (std::strcmp(tag, "p") == 0 || std::strcmp(tag, "h1") == 0)) {
    if (!c.current.empty()) c.paragraphs.push_back(c.current);
    c.capture = false;
  }
}
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  ZipFile epub(argv[1]);
  size_t len = 0;
  // CrossPoint's original ZIP reader can reuse its bounded file-stat index.
  if (!epub.loadAllFileStatSlims()) {
    std::fputs("Preview: unable to index EPUB ZIP entries\n", stderr);
    return 3;
  }
  auto* xml = epub.readFileToMemory("OEBPS/chapter1.xhtml", &len, true);
  if (!xml || !len) {
    std::fputs("Preview: unable to extract XHTML chapter from EPUB ZIP\n", stderr);
    std::free(xml);
    return 4;
  }
  Chapter chapter;
  XML_Parser parser = XML_ParserCreate(nullptr);
  assert(parser);
  XML_SetUserData(parser, &chapter);
  XML_SetElementHandler(parser, onStart, onEnd);
  XML_SetCharacterDataHandler(parser, onText);
  bool ok = XML_Parse(parser, reinterpret_cast<const char*>(xml),
                      static_cast<int>(len), XML_TRUE) == XML_STATUS_OK;
  XML_ParserFree(parser);
  std::free(xml);
  assert(ok && chapter.paragraphs.size() >= 2);

  HalDisplay display;
  display.begin();
  GfxRenderer gfx(display);
  gfx.setOrientation(GfxRenderer::LandscapeCounterClockwise);
  gfx.begin();
  EpdFont font(&notosans_12_regular);
  gfx.insertFont(1, EpdFontFamily(12, &font));
  FontDecompressor decoder;
  assert(decoder.init());
  FontCacheManager cache(gfx.getFontMap(), gfx.getSdCardFonts(), gfx.getTtfFonts());
  cache.setFontDecompressor(&decoder);
  gfx.setFontCacheManager(&cache);
  gfx.clearScreen(0xff);

  int y = 12;
  unsigned lines = 0;
  for (const auto& paragraph : chapter.paragraphs) {
    ParsedText layout(false, false, BlockStyle(), 0);
    std::istringstream input(paragraph);
    std::string word;
    while (input >> word) layout.addWord(word, EpdFontFamily::REGULAR);
    layout.layoutAndExtractLines(gfx, 1, 345,
        [&](std::unique_ptr<TextBlock> block, uint32_t) {
          if (y + 22 > 320) return;
          assert(block && block->valid());
          block->render(gfx, 1, 14, y);
          y += 22;
          ++lines;
        });
    y += 8;
  }
  assert(lines >= 3);
  const auto* pixels = display.getFrameBuffer();
  assert(pixels);
  std::FILE* out = std::fopen(argv[2], "wb");
  assert(out);
  assert(std::fprintf(out, "P5\n375 320\n255\n") > 0);
  unsigned ink = 0;
  for (int row = 0; row < 320; ++row) {
    for (int col = 0; col < 375; ++col) {
      const bool paper = (pixels[row * 47 + col / 8] & (0x80u >> (col & 7))) != 0;
      const uint8_t gray = paper ? 255 : 0;
      if (!paper) ++ink;
      assert(std::fwrite(&gray, 1, 1, out) == 1);
    }
  }
  assert(std::fclose(out) == 0);
  assert(ink > 200);
  std::printf("Original CrossPoint EPUB text-page: %u lines, %u ink pixels\n", lines, ink);
  return 0;
}
