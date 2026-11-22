// Q2 Pod theme colours: Q2_POPUP parsing (the three styles, Classic's 64
// stops, bad input) and the zoned present (base palette, zone colours,
// rounded corners, gradient rows).
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>

#include "Q2Display.hpp"
#include "Q2Theme.hpp"

int main() {
  using books::UiTheme;
  assert(!books::parseUiTheme(nullptr).active());
  assert(!books::parseUiTheme("").active());
  assert(!books::parseUiTheme("neon 000000 111111 222222 333333 444444 555555").active());
  assert(!books::parseUiTheme("modern 000000 111111").active());
  auto m = books::parseUiTheme("modern 1c1c1c 3a3a3a ffffff aaaaaa 0a84ff ffffff extra fields");
  assert(m.style == UiTheme::Style::Modern && m.box == 0x1c1c1c && m.pick == 0x0a84ff && m.pickText == 0xffffff);
  auto n = books::parseUiTheme("minimal 000000 4a4a4a ffffff 8e8e93 ffffff ffffff");
  assert(n.style == UiTheme::Style::Minimal && n.text == 0x8e8e93);
  std::string classic = "classic ffffff 5a595a 000000 000000 0080de ffffff";
  for (int i = 0; i < 64; ++i) {
    char c[8];
    std::snprintf(c, sizeof c, " %06x", i < 32 ? 0xffffff - i : 0x0000ff + i);
    classic += c;
  }
  auto c = books::parseUiTheme(classic.c_str());
  assert(c.style == UiTheme::Style::Classic && c.bar.size() == 32 && c.band.size() == 32);
  assert(c.bar[0] == 0xffffff && c.band[31] == 0x0000ff + 63);
  auto flat = books::parseUiTheme("classic ffffff 5a595a 000000 000000 0080de ffffff");
  assert(flat.bar.size() == 1 && flat.band.size() == 1);  // no stops: flat colours

  // Present: a 1-bit canvas with ink at (0,0) and (40,40).
  constexpr int W = books::Q2Display::Width, H = books::Q2Display::Height, RB = (W + 7) / 8;
  std::vector<uint8_t> bw(RB * H, 0);
  bw[0] |= 0x80;
  bw[40 * RB + 5] |= 0x80;  // x 40
  std::vector<uint32_t> fb(W * H);
  const uint32_t rows[2] = {0x111111, 0x222222};
  books::Q2Display::Zone zones[2] = {
      {30, 30, 40, 40, 10, 0xff0000, 0x00ff00, nullptr, 0},   // rounded, flat
      {0, 100, 10, 20, 0, 0x0000ff, 0, rows, 2},              // gradient rows
  };
  assert(books::Q2Display::presentThemed(bw.data(), bw.size(), fb.data(), fb.size(), 0x000000, 0xffffff, zones, 2));
  assert(fb[0] == 0xff000000u);                     // base ink
  assert(fb[1] == 0xffffffffu);                     // base paper
  assert(fb[40 * W + 40] == 0xffff0000u);           // zone ink
  assert(fb[50 * W + 50] == 0xff00ff00u);           // zone paper
  assert(fb[30 * W + 30] == 0xffffffffu);           // outside the rounded corner: base
  assert(fb[100 * W + 0] == 0xff111111u && fb[119 * W + 0] == 0xff222222u);  // gradient
  std::puts("Books Q2 Pod theme: PASS");
  return 0;
}
