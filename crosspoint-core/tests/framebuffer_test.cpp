#include <Q2Framebuffer.hpp>
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstring>
using crosspod::Q2Framebuffer;
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
  assert(!Q2Framebuffer::rotateARGB(logical.data(),logical.size(),
                                    panel.data(),panel.size()-5,PW*4));
  assert(!Q2Framebuffer::rotateARGB(logical.data(),logical.size(),
                                    panel.data(),panel.size(),PW*4-1));
  Q2Framebuffer framebuffer;
  assert(!framebuffer.isOpen());
  assert(!framebuffer.open("/dev/crosspod-does-not-exist"));
  return 0;
}
