// SPDX-License-Identifier: MIT
#pragma once

#include "FlowCover.hpp"
#include "JpegPreview.hpp"

namespace books {
inline bool decodeCoverImage(const uint8_t* bytes,size_t size,PngPreview& decoded) {
  if(size<8)return false;
  if(bytes[0]==0xff && bytes[1]==0xd8)return decodeJpegRgbPreview(bytes,size,decoded,true);
  return decodePngPreview(bytes,size,decoded,true);
}

inline bool makeColorCover(const PngPreview& image,FlowCover& cover) {
  return makeFlowCover(image.rgb.data(),image.rgb.size(),image.width,image.height,cover);
}
} // namespace books
