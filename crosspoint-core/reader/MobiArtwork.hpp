// SPDX-License-Identifier: MIT
#pragma once
#include "MobiDecode.hpp"
#include <zlib.h>

namespace books::mobipkg {
// CrossPoint renders PNG/JPEG. Convert only the first frame of a bounded GIF
// to PNG, so old Mobipocket artwork never requires a new device dependency.
inline void big32(std::string& s,uint32_t v) {for(int i=24;i>=0;i-=8)s+=char(v>>i);}
inline void pngChunk(std::string& s,const char* tag,std::string_view data) {
  big32(s,static_cast<uint32_t>(data.size()));const size_t start=s.size();s.append(tag,4);s.append(data);
  big32(s,static_cast<uint32_t>(::crc32(0,reinterpret_cast<const Bytef*>(s.data()+start),static_cast<uInt>(4+data.size()))));
}
inline bool gifToPng(std::string_view gif,std::string& png) {
  if(imageType(gif)!=3||gif.size()<13)return false;
  const auto le=[&](size_t at){return byte(gif,at)|(byte(gif,at+1)<<8);};
  size_t at=13;std::string_view palette;
  const auto colors=[&](unsigned flags){const size_t n=3u*(2u<<(flags&7));if(n>gif.size()-at)return false;palette=gif.substr(at,n);at+=n;return true;};
  if((byte(gif,10)&128)&&!colors(byte(gif,10)))return false;
  int transparent=-1;
  while(at<gif.size()){
    const unsigned tag=byte(gif,at++);
    if(tag==0x3b)return false;
    if(tag==0x21){
      if(at==gif.size())return false;
      const unsigned label=byte(gif,at++);
      if(label==0xf9&&gif.size()-at>=6&&byte(gif,at)==4)transparent=(byte(gif,at+1)&1)?int(byte(gif,at+4)):-1;
      while(at<gif.size()){const size_t n=byte(gif,at++);if(!n)break;if(n>gif.size()-at)return false;at+=n;}
      continue;
    }
    if(tag!=0x2c||gif.size()-at<9)return false;
    const unsigned w=le(at+4),h=le(at+6),flags=byte(gif,at+8);at+=9;
    if(!w||!h||w>2048||h>2048||size_t(w)*h>1024u*1024)return false;
    if((flags&128)&&!colors(flags))return false;
    if(palette.empty()||at==gif.size())return false;
    const unsigned min=byte(gif,at++);if(min<2||min>8)return false;
    std::string packed;
    while(at<gif.size()){const size_t n=byte(gif,at++);if(!n)break;if(n>gif.size()-at)return false;packed.append(gif.substr(at,n));at+=n;}
    const unsigned clear=1u<<min,end=clear+1;unsigned next=end+1,width=min+1,first=0;int old=-1;
    std::array<uint16_t,4096> prefix{};std::array<uint8_t,4096> suffix{},stack{};
    std::string pixels;pixels.reserve(size_t(w)*h);size_t bit=0;
    while(pixels.size()<size_t(w)*h&&bit+width<=packed.size()*8){
      unsigned code=0;for(unsigned k=0;k<width;++k,++bit)code|=((byte(packed,bit/8)>>(bit%8))&1u)<<k;
      if(code==clear){next=end+1;width=min+1;old=-1;continue;}
      if(code==end)break;
      const unsigned incoming=code;size_t count=0;
      if(code>=next){if(code!=next||old<0)return false;stack[count++]=static_cast<uint8_t>(first);code=static_cast<unsigned>(old);}
      while(code>=clear){if(code>=next||count>=stack.size()-1)return false;stack[count++]=suffix[code];code=prefix[code];}
      first=code;stack[count++]=static_cast<uint8_t>(code);
      if(count>size_t(w)*h-pixels.size())return false;
      while(count)pixels+=char(stack[--count]);
      if(old>=0&&next<4096){prefix[next]=static_cast<uint16_t>(old);suffix[next]=static_cast<uint8_t>(first);++next;if(next==(1u<<width)&&width<12)++width;}
      old=static_cast<int>(incoming);
    }
    if(pixels.size()!=size_t(w)*h)return false;
    std::string scan(size_t(h)*(size_t(w)*3+1),'\0');size_t p=0;
    const auto row=[&](unsigned y){size_t out=size_t(y)*(size_t(w)*3+1)+1;for(unsigned x=0;x<w;++x){const unsigned color=byte(pixels,p++);if(size_t(color)*3+2>=palette.size())return false;for(unsigned c=0;c<3;++c)scan[out++]=int(color)==transparent?char(255):palette[color*3+c];}return true;};
    if(flags&64){constexpr unsigned start[]={0,4,2,1},step[]={8,8,4,2};for(unsigned i=0;i<4;++i)for(unsigned y=start[i];y<h;y+=step[i])if(!row(y))return false;}
    else for(unsigned y=0;y<h;++y)if(!row(y))return false;
    uLongf n=compressBound(static_cast<uLong>(scan.size()));std::string compressed(n,'\0');
    if(compress2(reinterpret_cast<Bytef*>(compressed.data()),&n,reinterpret_cast<const Bytef*>(scan.data()),static_cast<uLong>(scan.size()),Z_BEST_SPEED)!=Z_OK)return false;
    compressed.resize(n);std::string header;big32(header,w);big32(header,h);header.append("\x08\x02\0\0\0",5);
    png="\x89PNG\r\n\x1a\n";pngChunk(png,"IHDR",header);pngChunk(png,"IDAT",compressed);pngChunk(png,"IEND",{});return true;
  }
  return false;
}
inline bool normalizeArtwork(std::string& data) {
  const int type=imageType(data);if(type==1||type==2)return true;
  if(type!=3)return false;
  std::string png;if(!gifToPng(data,png))return false;data=std::move(png);return true;
}
} // namespace books::mobipkg
