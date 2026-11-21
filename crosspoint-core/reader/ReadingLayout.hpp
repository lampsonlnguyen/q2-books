// SPDX-License-Identifier: MIT
#pragma once
// Q2 reader configuration -> original CrossPoint Section layout contract.
// Keep this separate from the fixed-size CrossPod UI/menu font.
#include "ReaderPreferences.hpp"
#include <ReaderRenderSpec.h>
#include <algorithm>
#include <cstdint>

namespace crosspod {
inline ReaderRenderSpec makeReaderRenderSpec(
    const ReaderPreferences& preferences,int readingFontId,
    uint16_t screenWidth,uint16_t contentHeight) {
  ReaderRenderSpec spec{};
  spec.fontId=readingFontId;
  spec.viewportWidth=static_cast<uint16_t>(
      std::max(1,static_cast<int>(screenWidth)-
          2*preferences.margin-2));
  spec.viewportHeight=contentHeight;
  spec.lineCompression=preferences.linePercent/100.0f;
  spec.paragraphAlignment=static_cast<uint8_t>(preferences.alignment);
  spec.hyphenationEnabled=preferences.hyphenation;
  spec.embeddedStyle=preferences.embeddedStyle;
  // 0 means CrossPoint's default monochrome image rendering on Q2;
  // image decoding and rasterization remain in the upstream engine.
  spec.imageRendering=0;
  return spec;
}
} // namespace crosspod
