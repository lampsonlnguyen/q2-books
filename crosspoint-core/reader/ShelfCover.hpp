// SPDX-License-Identifier: MIT
#pragma once
// Small monochrome EPUB cover thumbnails; no full-size image decoding during
// shelf startup or page turns. The decoder is the existing Q2 PNG/JPEG path.
#include "ReadingPosition.hpp"
#include "ShelfIdentity.hpp"
#include "PngPreview.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace books {
struct ShelfCover {
  static constexpr int width=48,height=60,stride=6;
  std::array<uint8_t,stride*height> ink{};
};
inline std::string shelfCoverFile(const std::string& book) {
  return positionFile(book)+".cover";
}
inline bool makeShelfCover(const PngPreview& source,ShelfCover& cover) {
  if(!source.width || !source.height || source.width>1048576/source.height ||
     source.gray.size()!=static_cast<size_t>(source.width)*source.height)
    return false;
  cover.ink.fill(0);
  const unsigned w=source.width,h=source.height;
  const unsigned scaledW=w*ShelfCover::height>h*ShelfCover::width
      ?ShelfCover::width:std::max(1u,w*ShelfCover::height/h);
  const unsigned scaledH=w*ShelfCover::height>h*ShelfCover::width
      ?std::max(1u,h*ShelfCover::width/w):ShelfCover::height;
  const unsigned left=(ShelfCover::width-scaledW)/2;
  const unsigned top=(ShelfCover::height-scaledH)/2;
  static constexpr uint8_t bayer[4][4]={
      {0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
  for(unsigned y=0;y<scaledH;++y) {
    const unsigned srcY=static_cast<unsigned>(
        static_cast<uint64_t>(y)*h/scaledH);
    for(unsigned x=0;x<scaledW;++x) {
      const unsigned srcX=static_cast<unsigned>(
          static_cast<uint64_t>(x)*w/scaledW);
      const uint8_t gray=source.gray[
          static_cast<size_t>(srcY)*w+srcX];
      if(gray<static_cast<unsigned>(bayer[y&3][x&3])*16u+8u) {
        const unsigned px=x+left,py=y+top;
        cover.ink[py*ShelfCover::stride+px/8]|=
            static_cast<uint8_t>(0x80u>>(px&7));
      }
    }
  }
  return true;
}
inline bool saveShelfCover(const std::string& book,const ShelfCover& cover) {
  struct stat st{};
  if(::stat(book.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  const std::string path=shelfCoverFile(book),tmp=path+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"BOOKS-COVER1 %llu %llu\n",
      static_cast<unsigned long long>(st.st_size),
      static_cast<unsigned long long>(st.st_mtime))>0;
  if(ok && std::fwrite(cover.ink.data(),1,cover.ink.size(),f)!=cover.ink.size())
    ok=false;
  if(std::fflush(f)!=0)ok=false;
  if(ok&&fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok&&std::rename(tmp.c_str(),path.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
}
inline bool loadShelfCover(const std::string& book,ShelfCover& cover) {
  struct stat st{};
  if(::stat(book.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  FILE* f=std::fopen(shelfCoverFile(book).c_str(),"rb");
  if(!f)return false;
  char header[128]{};
  if(!std::fgets(header,sizeof(header),f)) {std::fclose(f);return false;}
  unsigned long long bytes=0,changed=0;
  const bool matching=parseShelfIdentityHeader(
      header,"BOOKS-COVER1",bytes,changed) &&
      bytes==static_cast<unsigned long long>(st.st_size) &&
      changed==static_cast<unsigned long long>(st.st_mtime);
  if(!matching) {std::fclose(f);return false;}
  ShelfCover loaded;
  const bool ok=std::fread(loaded.ink.data(),1,loaded.ink.size(),f)
      ==loaded.ink.size();
  std::fclose(f);
  if(ok)cover=loaded;
  return ok;
}
} // namespace books
