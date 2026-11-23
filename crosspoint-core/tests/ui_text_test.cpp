// SPDX-License-Identifier: MIT
// UiText: Q2 Pod's TrueType faces through FreeType, drawn as 8-bit coverage
// into the display's smooth layer. Uses Liberation Sans (Q2 Pod's bold.ttf
// is cut from Liberation Sans Bold) as stand-ins; skips without them.
#include "UiText.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
bool exists(const std::string& p) {
  struct stat st {};
  return ::stat(p.c_str(), &st) == 0;
}
std::string find(const char* name) {
  const char* dirs[] = {"/usr/share/fonts/truetype/liberation", "/usr/share/fonts/truetype/liberation2",
                        "/opt/npm-tools/node_modules/pdfjs-dist/standard_fonts", "/tmp/uifonts"};
  for (const char* d : dirs)
    if (exists(std::string(d) + "/" + name)) return std::string(d) + "/" + name;
  return {};
}
}  // namespace

int main() {
  const std::string regular = find("LiberationSans-Regular.ttf"), bold = find("LiberationSans-Bold.ttf");
  if (regular.empty() || bold.empty()) {
    std::puts("UiText: Liberation Sans not found; skipped");
    return 77;
  }
  char tmpl[] = "/tmp/books-uifonts-XXXXXX";
  const std::string dir = ::mkdtemp(tmpl);
  assert(::symlink(regular.c_str(), (dir + "/default.ttf").c_str()) == 0);
  assert(::symlink(bold.c_str(), (dir + "/bold.ttf").c_str()) == 0);
  ::setenv("BOOKS_UI_FONT_DIR", dir.c_str(), 1);
  ::setenv("BOOKS_UI_PACK_FONT", "/nonexistent.ttf", 1);

  auto& ui = books::UiText::instance();
  ui.setThemeFace(books::UiText::Face::Regular);
  assert(ui.available());

  // Q2 Pod's px: ascent + descent = px.
  for (int px : {15, 21, 24, 27}) {
    const int h = ui.lineHeight(px);
    assert(h >= px - 1 && h <= px + 1);
    assert(ui.ascent(px) > px / 2 && ui.ascent(px) < px);
  }
  const int w15 = ui.width("Reading settings", 15), w24 = ui.width("Reading settings", 24);
  assert(w15 > 60 && w24 > w15 && w24 < 260);

  // Coverage: Latin and Vietnamese yes; CJK not in these faces, so the
  // caller falls back rather than drawing boxes.
  assert(ui.drawable("Nguyễn Thị Minh") && ui.has('A'));
  assert(!ui.drawable("漢字") && !ui.has(0x6f22));

  // The bold face comes first for Modern: different (wider) advances.
  ui.setThemeFace(books::UiText::Face::Bold);
  const int wBold = ui.width("Reading settings", 24);
  assert(wBold > w24);
  // Classic's face is missing here: Q2 Pod's order falls to bold.ttf.
  ui.setThemeFace(books::UiText::Face::Classic);
  assert(ui.available() && ui.width("Reading settings", 24) == wBold);
  ui.setThemeFace(books::UiText::Face::Regular);

  // Drawing: ink in the 1-bit canvas where coverage is at least half, and
  // true in-between coverage at the edges (not just black and white).
  HalDisplay display;
  display.clearScreen(0xff);
  const int end = ui.draw(display, 20, 40, "Smooth text", 24, 360);
  assert(end > 20 && end - 20 == ui.width("Smooth text", 24));
  std::vector<uint8_t> alpha(HalDisplay::PIXELS);
  assert(display.currentAlpha(alpha.data()));
  int ink = 0, edge = 0, outside = 0;
  for (int y = 0; y < HalDisplay::DISPLAY_HEIGHT; ++y)
    for (int x = 0; x < HalDisplay::DISPLAY_WIDTH; ++x) {
      const uint8_t a = alpha[size_t(y) * HalDisplay::DISPLAY_WIDTH + x];
      if (a == 255) ++ink;
      if (a > 0 && a < 255) ++edge;
      if (a && (y < 40 || y >= 40 + ui.lineHeight(24) + 2 || x < 18 || x > end + 2)) ++outside;
    }
  assert(ink > 100 && edge > 100 && outside == 0);
  const uint8_t* fb = display.getFrameBuffer();
  int bitsInk = 0;
  for (uint32_t i = 0; i < HalDisplay::BUFFER_SIZE; ++i) bitsInk += __builtin_popcount(uint8_t(~fb[i]));
  assert(bitsInk > ink);  // >= half coverage is ink for 1-bit readers too

  // A cleared screen drops the coverage.
  display.clearScreen(0xff);
  assert(display.currentAlpha(alpha.data()));
  for (uint8_t a : alpha) assert(a == 0);

  // Clipping at maxX: whole glyphs only.
  const int clipped = ui.draw(display, 20, 100, "A long label that does not fit", 24, 120);
  assert(clipped <= 121 && clipped > 20);

  std::printf("Books UiText: %d solid and %d edge pixels for \"Smooth text\" at 24 px: OK\n", ink, edge);
  ::unlink((dir + "/default.ttf").c_str());
  ::unlink((dir + "/bold.ttf").c_str());
  ::rmdir(dir.c_str());
  return 0;
}
