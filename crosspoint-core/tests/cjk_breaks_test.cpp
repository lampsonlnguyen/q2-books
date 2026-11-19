#include <CjkBreaks.h>
#include <TokenBoundary.h>
#include <hyphenation/HyphenationCommon.h>
#include <cassert>
#include <string>
int main() {
  assert(CjkBreaks::boundaryAllowsBreak(0x4E2D,0x6587)); // 中文
  assert(!CjkBreaks::boundaryAllowsBreak(0x4E2D,0x3002)); // 。 cannot start a line
  assert(!CjkBreaks::boundaryAllowsBreak(0x300C,0x4E2D)); // 「 cannot end a line
  assert(!CjkBreaks::hasBreakOpportunityBetween(0xAC00,0xB098)); // Hangul
  assert(TokenBoundary::allowsBreak(false,false));
  assert(!TokenBoundary::allowsBreak(true,false));
  assert(TokenBoundary::allowsBreak(true,true));
  const auto offsets = CjkBreaks::characterBreakByteOffsets("中文");
  assert(offsets.size() == 1 && offsets[0] == 3);
  assert(CjkBreaks::characterBreakByteOffsets("Hello").empty());
  assert(isExplicitHyphen('-'));
  assert(isSoftHyphen(0x00AD));
  const auto cps = collectCodepoints("café");
  assert(cps.size() == 4 && cps.back().value == 0xE9);
  return 0;
}
