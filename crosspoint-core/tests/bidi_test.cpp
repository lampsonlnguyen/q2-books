#include <BidiUtils.h>
#include <cassert>
#include <string>
#include <vector>

int main() {
  assert(BidiUtils::startsWithRtl("مرحبا بالعالم"));
  assert(BidiUtils::startsWithRtl("שלום עולם"));
  assert(!BidiUtils::startsWithRtl("Nguyễn và tiếng Việt"));
  assert(BidiUtils::detectParagraphLevel("مرحبا", 0) == 1);
  assert(BidiUtils::detectParagraphLevel("Hello", 1) == 0);
  assert(BidiUtils::isTransparentMark(0x05B0)); // Hebrew niqqud
  assert(BidiUtils::isTransparentMark(0x064E)); // Arabic fatha
  assert(!BidiUtils::isTransparentMark(0x0301)); // Latin acute accent
  std::string visual;
  assert(BidiUtils::applyBidiVisual("مرحبا", visual));
  assert(!visual.empty());
  std::string unchanged = "preserved";
  assert(!BidiUtils::applyBidiVisual("", unchanged));
  assert(unchanged == "preserved");
  std::vector<std::string> words = {"English", "שלום", "123"};
  std::vector<uint16_t> order;
  assert(BidiUtils::computeVisualWordOrder(words, false, order));
  assert(order.size() == words.size());
  for (unsigned i=0; i<order.size(); ++i)
    for (unsigned j=i+1; j<order.size(); ++j)
      assert(order[i] != order[j]);
  return 0;
}
