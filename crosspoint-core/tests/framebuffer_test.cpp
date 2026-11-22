#include <Q2Framebuffer.hpp>
#include <Q2PagePresenter.hpp>
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstring>
using books::Q2Framebuffer;
int main() {
  constexpr unsigned W = Q2Framebuffer::LogicalWidth;
  constexpr unsigned H = Q2Framebuffer::LogicalHeight;
  constexpr unsigned PW = Q2Framebuffer::PanelWidth;
  constexpr unsigned PH = Q2Framebuffer::PanelHeight;
  std::vector<uint32_t> logical(W*H, 0xff112233);
  std::vector<uint8_t> panel(PW*PH*4, 0);
  logical[0] = 0xffaabbcc;            // top-left -> panel (319,0)
  logical[W-1] = 0xffddee22;          // top-right -> panel (319,374)
  logical[(H-1)*W] = 0xff345678;      // bottom-left -> panel (0,0)
  logical.back() = 0xff8899aa;       // bottom-right -> panel (0,374)
  assert(Q2Framebuffer::rotateARGB(logical.data(),logical.size(),panel.data(),
                                    panel.size(),PW*4));
  auto at = [&](unsigned x, unsigned y) {
    uint32_t v = 0;
    std::memcpy(&v, panel.data() + (y * PW + x)*4, 4);
    return v;
  };
  assert(at(319,0)==0xffaabbcc);
  assert(at(319,374)==0xffddee22);
  assert(at(0,0)==0xff345678);
  assert(at(0,374)==0xff8899aa);
  assert(at(160,200)==0xff112233);
  // Books Test V3 was confirmed readable on physical Q2 hardware.
  // Its addressing rule writes pixel (logical_x, logical_y) to
  // panel row logical_x, column (319 - logical_y).
  // Check an asymmetric three-point 'L' to prevent mirrored text regressions.
  std::vector<uint32_t> asymmetric(W*H, 0xff000000);
  asymmetric[10*W+10] = 0xffff0000;
  asymmetric[10*W+11] = 0xff00ff00;
  asymmetric[11*W+10] = 0xff0000ff;
  assert(Q2Framebuffer::rotateARGB(asymmetric.data(),asymmetric.size(),
                                    panel.data(),panel.size(),PW*4));
  assert(at(309,10) == 0xffff0000);
  assert(at(309,11) == 0xff00ff00);
  assert(at(308,10) == 0xff0000ff);
  assert(at(308,11) == 0xff000000);

  assert(!Q2Framebuffer::rotateARGB(logical.data(),logical.size(),
                                    panel.data(),panel.size()-5,PW*4));
  assert(!Q2Framebuffer::rotateARGB(logical.data(),logical.size(),
                                    panel.data(),panel.size(),PW*4-1));
  Q2Framebuffer framebuffer;
  assert(!framebuffer.isOpen());
  assert(!framebuffer.open("/dev/books-does-not-exist"));
  books::Q2PagePresenter presenter;
  assert(!presenter.presentARGB(logical.data(), logical.size()));
  assert(!presenter.open("/dev/books-does-not-exist"));
  assert(!presenter.presentBW(nullptr, 0));
  return 0;
}
