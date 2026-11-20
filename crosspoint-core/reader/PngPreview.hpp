#pragma once
// SPDX-License-Identifier: MIT
// Transitional bounded PNG-to-monochrome adapter for the Q2 preview reader.
// The finished port should replace this with CrossPoint ImageBlock/decoders.
#include <zlib.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace crosspod {
struct PngPreview {
  unsigned width = 0, height = 0;
  std::vector<uint8_t> gray;
};
inline uint32_t readBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline bool decodePngPreview(const uint8_t* bytes,size_t len,PngPreview& out) {
  out = {};
  constexpr size_t MAX_COMPRESSED = 4u*1024u*1024u;
  constexpr uint32_t MAX_PIXELS = 1024u*1024u;
  static const uint8_t sig[8]={137,80,78,71,13,10,26,10};
  if(!bytes||len<33||std::memcmp(bytes,sig,8)!=0)return false;
  size_t at=8;
  uint32_t width=0,height=0;
  unsigned channels=0;
  bool seenIHDR=false,seenEND=false;
  std::vector<uint8_t> compressed;
  while(at+12<=len) {
    const uint32_t amount=readBE32(bytes+at);
    if(amount>len-at-12)return false;
    const uint8_t* type=bytes+at+4;
    const uint8_t* chunk=bytes+at+8;
    const uLong calculated=crc32(crc32(0L,Z_NULL,0),type,4);
    if(crc32(calculated,chunk,amount)!=readBE32(chunk+amount))return false;
    if(std::memcmp(type,"IHDR",4)==0) {
      if(seenIHDR || amount!=13 || at!=8)return false;
      seenIHDR=true;
      width=readBE32(chunk);
      height=readBE32(chunk+4);
      if(width==0||height==0||width>MAX_PIXELS/height || chunk[8]!=8 ||
         (chunk[9]!=2 && chunk[9]!=6) || chunk[10]!=0 ||
         chunk[11]!=0 || chunk[12]!=0)return false;
      channels=chunk[9]==2?3u:4u;
    } else if(std::memcmp(type,"IDAT",4)==0) {
      if(!seenIHDR || compressed.size()>MAX_COMPRESSED-std::min<size_t>(amount,MAX_COMPRESSED) ||
         amount>MAX_COMPRESSED-compressed.size())return false;
      compressed.insert(compressed.end(),chunk,chunk+amount);
    } else if(std::memcmp(type,"IEND",4)==0) {
      seenEND=amount==0;
      break;
    } else if((type[0]&0x20u)==0) {
      // Unknown mandatory PNG chunk.
      return false;
    }
    at+=12u+amount;
  }
  if(!seenIHDR||!seenEND||compressed.empty())return false;
  const size_t stride=static_cast<size_t>(width)*channels;
  const size_t rawLen=static_cast<size_t>(height)*(stride+1);
  if(rawLen>5u*1024u*1024u)return false;
  std::vector<uint8_t> raw(rawLen);
  uLongf actual=rawLen;
  if(uncompress(raw.data(),&actual,compressed.data(),compressed.size())!=Z_OK ||
     actual!=rawLen)return false;
  out.width=width;
  out.height=height;
  out.gray.resize(static_cast<size_t>(width)*height);
  std::vector<uint8_t> previous(stride,0);
  for(uint32_t y=0;y<height;++y) {
    uint8_t* row=raw.data()+static_cast<size_t>(y)*(stride+1)+1;
    const unsigned filter=row[-1];
    if(filter>4)return false;
    for(size_t x=0;x<stride;++x) {
      const int left=x>=channels?row[x-channels]:0;
      const int above=previous[x];
      const int upperLeft=x>=channels?previous[x-channels]:0;
      int predictor=0;
      switch(filter) {
        case 1:predictor=left;break;
        case 2:predictor=above;break;
        case 3:predictor=(left+above)/2;break;
        case 4:{
          const int p=left+above-upperLeft;
          const int a=std::abs(p-left),b=std::abs(p-above),c=std::abs(p-upperLeft);
          predictor=a<=b&&a<=c?left:(b<=c?above:upperLeft);
          break;
        }
      }
      row[x]=static_cast<uint8_t>(row[x]+predictor);
    }
    for(uint32_t x=0;x<width;++x) {
      const uint8_t* pixel=row+static_cast<size_t>(x)*channels;
      const uint32_t gray=(77u*pixel[0]+150u*pixel[1]+29u*pixel[2]+128u)>>8;
      const uint32_t alpha=channels==4?pixel[3]:255;
      out.gray[static_cast<size_t>(y)*width+x]=static_cast<uint8_t>(
        (gray*alpha+255u*(255u-alpha)+127u)/255u);
    }
    std::memcpy(previous.data(),row,stride);
  }
  return true;
}
} // namespace crosspod
