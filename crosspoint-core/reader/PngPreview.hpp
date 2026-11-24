#pragma once
// SPDX-License-Identifier: MIT
// Bounded PNG decoder for the transitional Books Q2 reader.
// Supports the common EPUB PNG variants: 8-bit grayscale, indexed palette,
// grayscale+alpha, RGB and RGBA (non-interlaced only). Uses original
// CrossPoint EPUB/ZIP and GfxRenderer; this is not a separate UI framework.
#include <zlib.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <vector>

namespace books {
struct PngPreview {
  unsigned width = 0, height = 0;
  std::vector<uint8_t> gray;
  std::vector<uint8_t> rgb;
};
inline uint32_t readBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
inline bool decodePngPreview(const uint8_t* bytes,size_t len,PngPreview& out,bool color=false) {
  out = {};
  constexpr size_t MAX_COMPRESSED = 4u*1024u*1024u;
  constexpr uint32_t MAX_PIXELS = 1024u*1024u;
  static const uint8_t sig[8]={137,80,78,71,13,10,26,10};
  if(!bytes||len<33||std::memcmp(bytes,sig,8)!=0)return false;
  size_t at=8;
  uint32_t width=0,height=0;
  unsigned channels=0,colorType=255;
  bool seenIHDR=false,seenEND=false,seenIDAT=false,seenPLTE=false,seenTRNS=false;
  std::array<uint8_t,768> palette{};
  std::array<uint8_t,256> paletteAlpha{};
  paletteAlpha.fill(255);
  size_t paletteEntries=0;
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
         chunk[10]!=0 || chunk[11]!=0 || chunk[12]!=0)return false;
      colorType=chunk[9];
      switch(colorType) {
        case 0:channels=1;break;  // 8-bit grayscale
        case 2:channels=3;break;  // 8-bit RGB
        case 3:channels=1;break;  // 8-bit indexed palette
        case 4:channels=2;break;  // 8-bit grayscale + alpha
        case 6:channels=4;break;  // 8-bit RGBA
        default:return false;
      }
    } else if(std::memcmp(type,"PLTE",4)==0) {
      if(!seenIHDR||seenIDAT||seenPLTE||amount==0||
         amount>palette.size()||amount%3!=0)return false;
      seenPLTE=true;
      paletteEntries=amount/3;
      std::memcpy(palette.data(),chunk,amount);
    } else if(std::memcmp(type,"tRNS",4)==0) {
      if(!seenIHDR||seenIDAT||seenTRNS||colorType!=3||
         !seenPLTE||amount>paletteEntries)return false;
      seenTRNS=true;
      std::memcpy(paletteAlpha.data(),chunk,amount);
    } else if(std::memcmp(type,"IDAT",4)==0) {
      if(!seenIHDR || (colorType==3&&!seenPLTE)||
         amount>MAX_COMPRESSED-compressed.size())return false;
      seenIDAT=true;
      compressed.insert(compressed.end(),chunk,chunk+amount);
    } else if(std::memcmp(type,"IEND",4)==0) {
      seenEND=amount==0;
      break;
    } else if((type[0]&0x20u)==0) {
      return false; // unknown mandatory PNG chunk
    }
    at+=12u+amount;
  }
  if(!seenIHDR||!seenEND||!seenIDAT||compressed.empty())return false;
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
  if(color)out.rgb.resize(static_cast<size_t>(width)*height*3);
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
      unsigned gray=0,alpha=255;
      unsigned red=0,green=0,blue=0;
      if(colorType==0 || colorType==4) {
        gray=pixel[0];
        red=green=blue=gray;
        if(colorType==4)alpha=pixel[1];
      } else {
        const uint8_t* rgb=pixel;
        if(colorType==3) {
          if(pixel[0]>=paletteEntries)return false;
          rgb=palette.data()+3u*pixel[0];
          alpha=paletteAlpha[pixel[0]];
        } else if(colorType==6)alpha=pixel[3];
        red=rgb[0];green=rgb[1];blue=rgb[2];
        gray=(77u*rgb[0]+150u*rgb[1]+29u*rgb[2]+128u)>>8;
      }
      if(color) {
        const size_t offset=3*(static_cast<size_t>(y)*width+x);
        const unsigned channelsRgb[]={red,green,blue};
        for(unsigned c=0;c<3;++c)
          out.rgb[offset+c]=static_cast<uint8_t>((channelsRgb[c]*alpha+255u*(255u-alpha)+127u)/255u);
      }
      out.gray[static_cast<size_t>(y)*width+x]=static_cast<uint8_t>(
        (gray*alpha+255u*(255u-alpha)+127u)/255u);
    }
    std::memcpy(previous.data(),row,stride);
  }
  return true;
}
} // namespace books
