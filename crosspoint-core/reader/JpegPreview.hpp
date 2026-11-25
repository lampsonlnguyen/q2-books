#pragma once
// SPDX-License-Identifier: MIT
// C/libjpeg decoding is isolated from the C++ reader's allocation lifetime.
#include "PngPreview.hpp"
#include <cstdlib>
#include <cstddef>
#include <cstdint>
extern "C" int booksDecodeJpeg(const unsigned char*,size_t,
                                  unsigned char**,unsigned*,unsigned*);
extern "C" int booksDecodeJpegRgb(const unsigned char*,size_t,
                                     unsigned char**,unsigned*,unsigned*);
extern "C" int booksDecodeJpegCover(const unsigned char*,size_t,
                                       unsigned char**,unsigned*,unsigned*);
namespace books {
inline bool decodeJpegRgbPreview(const uint8_t* bytes,size_t length,PngPreview& out,bool cover=false) {
  out={};
  unsigned char* raw=nullptr;
  unsigned w=0,h=0;
  const auto decode=cover?booksDecodeJpegCover:booksDecodeJpegRgb;
  if(!decode(bytes,length,&raw,&w,&h))return false;
  if(!raw||!w||!h||w>1048576u/h) {std::free(raw);return false;}
  out.width=w;out.height=h;
  out.rgb.assign(raw,raw+static_cast<size_t>(w)*h*3);
  std::free(raw);
  return true;
}
inline bool decodeJpegPreview(const uint8_t* bytes,size_t length,PngPreview& out) {
  out={};
  unsigned char* raw=nullptr;
  unsigned w=0,h=0;
  if(!booksDecodeJpeg(bytes,length,&raw,&w,&h))return false;
  if(!raw||!w||!h||w>1048576u/h) {std::free(raw);return false;}
  out.width=w;out.height=h;
  out.gray.assign(raw,raw+static_cast<size_t>(w)*h);
  std::free(raw);
  return true;
}
} // namespace books
