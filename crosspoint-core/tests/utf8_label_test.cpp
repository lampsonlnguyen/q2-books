// SPDX-License-Identifier: MIT
#include "Utf8Label.hpp"
#include <cassert>
#include <string>
int main() {
  assert(crosspod::clippedUiLabel("Chapter One",7)=="Chapter...");
  assert(crosspod::clippedUiLabel(u8"Cafe\u0301 and books",4)==u8"Café...");
  assert(crosspod::clippedUiLabel(u8"Tiếng Việt",5)==u8"Tiếng...");
  assert(crosspod::clippedUiLabel(u8"日本語タイトル",2)==u8"日本...");
  assert(crosspod::clippedUiLabel(u8"한글책",2)==u8"한글...");
  assert(crosspod::clippedUiLabel(u8"العربية",3)==u8"العر...");
  assert(crosspod::clippedUiLabel(u8"עברית",3)==u8"עבר...");
  assert(crosspod::clippedUiLabel(u8"नमस्ते",100)==u8"नमस्ते");
  return 0;
}
