// SPDX-License-Identifier: MIT
// Smooth text: grey planes from the engine's two grayscale passes become
// coverage levels, only where the screen still shows the rendered page.
#include "Q2Display.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>

using books::Q2Display;

namespace {
constexpr size_t kBytes = Q2Display::RowBytes * Q2Display::Height;
void setBit(std::vector<uint8_t>& plane, int x, int y) {
  plane[size_t(y) * Q2Display::RowBytes + unsigned(x) / 8] |= uint8_t(0x80u >> (x & 7));
}
void clearBit(std::vector<uint8_t>& plane, int x, int y) {
  plane[size_t(y) * Q2Display::RowBytes + unsigned(x) / 8] &= uint8_t(~(0x80u >> (x & 7)));
}
uint8_t at(const std::vector<uint8_t>& levels, int x, int y) { return levels[size_t(y) * Q2Display::Width + x]; }
}  // namespace

int main() {
  // The decode table: solid ink, dark grey, light grey, paper.
  assert(Q2Display::coverage(1, 0, 0) == 3);
  assert(Q2Display::coverage(1, 1, 1) == 2);
  assert(Q2Display::coverage(1, 0, 1) == 1);
  assert(Q2Display::coverage(0, 1, 1) == 0);  // a grey bit never inks paper

  std::vector<uint8_t> page(kBytes, 0), lsb(kBytes, 0), msb(kBytes, 0);
  std::vector<uint8_t> levels(size_t(Q2Display::Width) * Q2Display::Height, 9);
  // Page ink at x=10 (solid), 11 (dark), 12 (light).
  for (int x : {10, 11, 12}) setBit(page, x, 5);
  setBit(msb, 11, 5); setBit(lsb, 11, 5);
  setBit(msb, 12, 5);
  // A stray grey bit on paper (x=13) must not show.
  setBit(msb, 13, 5);

  std::vector<uint8_t> screen = page;
  Q2Display::computeLevels(screen.data(), page.data(), lsb.data(), msb.data(), false, levels.data());
  assert(at(levels, 10, 5) == 3 && at(levels, 11, 5) == 2 && at(levels, 12, 5) == 1);
  assert(at(levels, 13, 5) == 0 && at(levels, 0, 0) == 0);

  // An overlay that inks a paper pixel, or clears an ink pixel, shows plain.
  setBit(screen, 13, 5);
  clearBit(screen, 12, 5);
  Q2Display::computeLevels(screen.data(), page.data(), lsb.data(), msb.data(), false, levels.data());
  assert(at(levels, 13, 5) == 3);  // overlay ink: solid, not the stray grey
  assert(at(levels, 12, 5) == 0);  // overlay paper
  assert(at(levels, 11, 5) == 2);  // the rest of the page keeps its levels

  // Night mode inverts the whole screen: levels flip to the paper's share.
  std::vector<uint8_t> night(kBytes);
  for (size_t i = 0; i < kBytes; ++i) night[i] = uint8_t(~page[i]);
  Q2Display::computeLevels(night.data(), page.data(), lsb.data(), msb.data(), true, levels.data());
  assert(at(levels, 10, 5) == 0 && at(levels, 11, 5) == 1 && at(levels, 12, 5) == 2);
  assert(at(levels, 0, 0) == 3);  // the page background is ink in night mode
  // An overlay drawn before the inversion (the progress bar) stays plain:
  // its pixel equals the page there, which in night mode means "changed".
  night[0] = page[0];
  Q2Display::computeLevels(night.data(), page.data(), lsb.data(), msb.data(), true, levels.data());
  assert(at(levels, 0, 0) == 0);

  // Blending: the ends are exact, the greys sit in between in order.
  const uint32_t paper = 0xf0e0d0, ink = 0x102030;
  assert(Q2Display::blend(paper, ink, 0) == (0xff000000u | paper));
  assert(Q2Display::blend(paper, ink, 3) == (0xff000000u | ink));
  const uint32_t light = Q2Display::blend(paper, ink, 1), dark = Q2Display::blend(paper, ink, 2);
  for (int shift : {16, 8, 0}) {
    const unsigned p = (paper >> shift) & 0xff, l = (light >> shift) & 0xff, d = (dark >> shift) & 0xff,
                   i = (ink >> shift) & 0xff;
    assert(p > l && l > d && d > i);
  }

  // Without levels the presenter is plain black and white, as before.
  std::vector<uint32_t> frame(size_t(Q2Display::Width) * Q2Display::Height);
  assert(Q2Display::presentThemed(page.data(), kBytes, frame.data(), frame.size(), ink, paper, nullptr, 0));
  assert(frame[5 * Q2Display::Width + 12] == (0xff000000u | ink));
  Q2Display::computeLevels(page.data(), page.data(), lsb.data(), msb.data(), false, levels.data());
  assert(Q2Display::presentThemed(page.data(), kBytes, frame.data(), frame.size(), ink, paper, nullptr, 0,
                                  levels.data()));
  assert(frame[5 * Q2Display::Width + 12] == light && frame[5 * Q2Display::Width + 11] == dark);

  std::puts("Books smooth text levels and blending: OK");
  return 0;
}
