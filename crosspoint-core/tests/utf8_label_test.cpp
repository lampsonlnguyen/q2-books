// SPDX-License-Identifier: MIT
#include "Utf8Label.hpp"
#include <cassert>
#include <string>
int main() {
  assert(books::clippedUiLabel("Chapter One",7)=="Chapter...");
  assert(books::clippedUiLabel(u8"Cafe\u0301 and books",4)==u8"Café...");
  assert(books::clippedUiLabel(u8"Tiếng Việt",5)==u8"Tiếng...");
  assert(books::clippedUiLabel(u8"日本語タイトル",2)==u8"日本...");
  assert(books::clippedUiLabel(u8"한글책",2)==u8"한글...");
  assert(books::clippedUiLabel(u8"العربية",3)==u8"الع...");
  assert(books::clippedUiLabel(u8"עברית",3)==u8"עבר...");
  assert(books::clippedUiLabel(u8"नमस्ते",100)==u8"नमस्ते");
  return 0;
}
