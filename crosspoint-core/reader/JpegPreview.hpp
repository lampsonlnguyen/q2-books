#pragma once
// SPDX-License-Identifier: MIT
// C/libjpeg decoding is isolated from the C++ reader's allocation lifetime.
#include "PngPreview.hpp"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
extern "C" int crosspodDecodeJpeg(const unsigned char*,size_t,
                                  unsigned char**,unsigned*,unsigned*);
namespace crosspod {
inline bool decodeJpegPreview(const uint8_t* bytes,size_t length,PngPreview& out) {
  out={};
  unsigned char* raw=nullptr;
  unsigned w=0,h=0;
  if(!crosspodDecodeJpeg(bytes,length,&raw,&w,&h))return false;
  if(!raw||!w||!h||w>1048576u/h) {std::free(raw);return false;}
  out.width=w;out.height=h;
  out.gray.assign(raw,raw+static_cast<size_t>(w)*h);
  std::free(raw);
  return true;
}
} // namespace crosspod
