// SPDX-License-Identifier: MIT
// Smooth text: the coverage layer (alpha + the state it was drawn with),
// the engine's grey planes decoded into it, and gamma-tuned blending.
#include "Q2Display.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

using books::Q2Display;

namespace {
constexpr size_t kBytes = Q2Display::RowBytes * Q2Display::Height;
constexpr size_t kPixels = size_t(Q2Display::Width) * Q2Display::Height;
void setBit(std::vector<uint8_t>& plane, int x, int y) {
  plane[size_t(y) * Q2Display::RowBytes + unsigned(x) / 8] |= uint8_t(0x80u >> (x & 7));
}
void clearBit(std::vector<uint8_t>& plane, int x, int y) {
  plane[size_t(y) * Q2Display::RowBytes + unsigned(x) / 8] &= uint8_t(~(0x80u >> (x & 7)));
}
size_t at(int x, int y) { return size_t(y) * Q2Display::Width + x; }
unsigned channel(uint32_t c, int shift) { return (c >> shift) & 0xff; }
}  // namespace

int main() {
  // The planes' decode: solid ink, dark grey, light grey, paper.
  assert(Q2Display::coverage(1, 0, 0) == 3);
  assert(Q2Display::coverage(1, 1, 1) == 2);
  assert(Q2Display::coverage(1, 0, 1) == 1);
  assert(Q2Display::coverage(0, 1, 1) == 0);  // a grey bit never inks paper
  assert(Q2Display::planeAlpha(1, 0, 0) == 255 && Q2Display::planeAlpha(1, 1, 1) == 170 &&
         Q2Display::planeAlpha(1, 0, 1) == 85 && Q2Display::planeAlpha(0, 0, 1) == 0);

  // A page: ink at x=10..12, coverage solid, 170 and 85; a TrueType fringe
  // on paper at x=13 (alpha 60, drawn as paper).
  std::vector<uint8_t> ink(kBytes, 0), alpha(kPixels, 0), expect(kPixels, Q2Display::kExpectPaper),
      out(kPixels, 7);
  for (int x : {10, 11, 12}) {
    setBit(ink, x, 5);
    expect[at(x, 5)] = Q2Display::kExpectInk;
  }
  alpha[at(10, 5)] = 255;
  alpha[at(11, 5)] = 170;
  alpha[at(12, 5)] = 85;
  alpha[at(13, 5)] = 60;
  Q2Display::computeAlpha(ink.data(), alpha.data(), expect.data(), out.data());
  assert(out[at(10, 5)] == 255 && out[at(11, 5)] == 170 && out[at(12, 5)] == 85);
  assert(out[at(13, 5)] == 60 && out[at(0, 0)] == 0);

  // Drawn over since: a pixel whose ink state changed shows plain.
  setBit(ink, 13, 5);    // overlay ink on the fringe
  clearBit(ink, 12, 5);  // overlay paper on a grey ink pixel
  Q2Display::computeAlpha(ink.data(), alpha.data(), expect.data(), out.data());
  assert(out[at(13, 5)] == 255 && out[at(12, 5)] == 0 && out[at(11, 5)] == 170);

  // No coverage (a cleared screen): plain black and white.
  std::vector<uint8_t> none(kPixels, Q2Display::kExpectNone);
  Q2Display::computeAlpha(ink.data(), alpha.data(), none.data(), out.data());
  assert(out[at(11, 5)] == 255 && out[at(0, 0)] == 0);

  // Blending: ends exact; greys in order; edges raised by the gamma, more
  // for light text on a dark page.
  const uint32_t white = 0xffffff, black = 0x000000;
  assert(Q2Display::blend(white, black, 0) == (0xff000000u | white));
  assert(Q2Display::blend(white, black, 255) == (0xff000000u | black));
  unsigned last = 256;
  for (unsigned a : {32u, 85u, 128u, 170u, 220u}) {
    const unsigned v = channel(Q2Display::blend(white, black, a), 8);
    assert(v < last);
    last = v;
  }
  // Dark text on light: an edge at half coverage is darker than a plain mix.
  assert(channel(Q2Display::blend(white, black, 128), 8) < 127);
  // Light text on dark gets the stronger raise.
  const unsigned darkOnLight = 255 - channel(Q2Display::blend(white, black, 85), 8);
  const unsigned lightOnDark = channel(Q2Display::blend(black, white, 85), 8);
  assert(lightOnDark > darkOnLight && lightOnDark > 85);

  // The presenter: plain without alpha, blended with it.
  std::vector<uint32_t> frame(kPixels);
  std::vector<uint8_t> page(kBytes, 0);
  setBit(page, 11, 5);
  assert(Q2Display::presentThemed(page.data(), kBytes, frame.data(), frame.size(), black, white, nullptr, 0));
  assert(frame[at(11, 5)] == (0xff000000u | black) && frame[at(0, 0)] == (0xff000000u | white));
  std::vector<uint8_t> shown(kPixels, 0);
  shown[at(11, 5)] = 170;
  assert(Q2Display::presentThemed(page.data(), kBytes, frame.data(), frame.size(), black, white, nullptr, 0,
                                  shown.data()));
  assert(frame[at(11, 5)] == Q2Display::blend(white, black, 170));

  std::puts("Books smooth text coverage and blending: OK");
  return 0;
}
