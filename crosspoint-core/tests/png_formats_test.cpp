// SPDX-License-Identifier: MIT
// Small deterministic PNG fixtures: exercise Books's real bounded image
// decoder without image libraries on the host or Q2.
#include <PngPreview.hpp>
#include <zlib.h>
#include <cassert>
#include <cstdint>
#include <vector>
#include <cstring>

using Bytes=std::vector<uint8_t>;
void be32(Bytes& out,uint32_t v) {
  out.push_back(uint8_t(v>>24));out.push_back(uint8_t(v>>16));
  out.push_back(uint8_t(v>>8));out.push_back(uint8_t(v));
}
void chunk(Bytes& png,const char* type,const Bytes& data) {
  be32(png,static_cast<uint32_t>(data.size()));
  const size_t offset=png.size();
  png.insert(png.end(),type,type+4);
  png.insert(png.end(),data.begin(),data.end());
  uLong crc=crc32(0,Z_NULL,0);
  crc=crc32(crc,png.data()+offset,static_cast<uInt>(png.size()-offset));
  be32(png,static_cast<uint32_t>(crc));
}
Bytes fixture(uint8_t color,const Bytes& scanline,const Bytes& palette={},
              const Bytes& transparency={}) {
  Bytes png={137,80,78,71,13,10,26,10};
  Bytes hdr;
  be32(hdr,2);be32(hdr,1);
  hdr.insert(hdr.end(),{8,color,0,0,0});
  chunk(png,"IHDR",hdr);
  if(!palette.empty())chunk(png,"PLTE",palette);
  if(!transparency.empty())chunk(png,"tRNS",transparency);
  uLongf cap=compressBound(scanline.size());
  Bytes zipped(cap);
  assert(compress2(zipped.data(),&cap,scanline.data(),scanline.size(),Z_BEST_SPEED)==Z_OK);
  zipped.resize(cap);
  chunk(png,"IDAT",zipped);
  chunk(png,"IEND",{});
  return png;
}
void check(const Bytes& png,uint8_t left,uint8_t right) {
  books::PngPreview image;
  assert(books::decodePngPreview(png.data(),png.size(),image));
  assert(image.width==2 && image.height==1);
  assert(image.gray.size()==2);
  assert(image.gray[0]==left && image.gray[1]==right);
}
int main() {
  check(fixture(0,{0,0,255}),0,255);                 // grayscale
  check(fixture(4,{0,0,255,0,0}),0,255);              // grayscale-alpha
  check(fixture(3,{0,0,1},{0,0,0,255,255,255}),0,255); // indexed
  check(fixture(3,{0,0,1},{0,0,0,0,0,0},{255,0}),0,255); // indexed tRNS
  check(fixture(2,{0,0,0,0,255,255,255}),0,255);      // RGB
  check(fixture(6,{0,0,0,0,255,0,0,0,0}),0,255);      // RGBA
  auto malformed=fixture(3,{0,3,1},{0,0,0,255,255,255});
  books::PngPreview ignored;
  assert(!books::decodePngPreview(malformed.data(),malformed.size(),ignored));
  malformed=fixture(0,{0,0,255});
  malformed[malformed.size()-1]^=1; // invalid IEND CRC
  assert(!books::decodePngPreview(malformed.data(),malformed.size(),ignored));
  return 0;
}
