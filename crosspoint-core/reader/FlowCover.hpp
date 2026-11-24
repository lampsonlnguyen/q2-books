// SPDX-License-Identifier: MIT
#pragma once
// Bounded RGB565 thumbnails for the graphical shelf; independent of legacy covers.
#include "ReadingPosition.hpp"
#include "ShelfIdentity.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>

namespace books {
struct FlowCover {
  static constexpr unsigned maxWidth=144,maxHeight=200;
  unsigned width=0,height=0;
  std::vector<uint16_t> pixels;
  bool valid() const {
    return width && height && width<=maxWidth && height<=maxHeight &&
           pixels.size()==size_t(width)*height;
  }
};
inline bool makeFlowCover(const uint8_t* rgb,size_t bytes,unsigned w,unsigned h,FlowCover& out) {
  if(!rgb || !w || !h || w>4194304u/h || bytes!=size_t(w)*h*3) return false;
  FlowCover next;
  if(uint64_t(w)*FlowCover::maxHeight>uint64_t(h)*FlowCover::maxWidth) {
    next.width=std::min(w,FlowCover::maxWidth);
    next.height=std::max(1u,unsigned(uint64_t(h)*next.width/w));
  } else {
    next.height=std::min(h,FlowCover::maxHeight);
    next.width=std::max(1u,unsigned(uint64_t(w)*next.height/h));
  }
  next.pixels.resize(size_t(next.width)*next.height);
  for(unsigned y=0;y<next.height;++y) for(unsigned x=0;x<next.width;++x) {
    const auto* p=rgb+3*(size_t(uint64_t(y)*h/next.height)*w+uint64_t(x)*w/next.width);
    next.pixels[size_t(y)*next.width+x]=uint16_t((p[0]&248u)<<8 | (p[1]&252u)<<3 | p[2]>>3);
  }
  out=std::move(next);
  return true;
}
inline std::string flowCoverFile(const std::string& book) {return positionFile(book)+".flowcover";}
inline bool saveFlowCover(const std::string& book,const FlowCover& cover) {
  struct stat st{};
  if(!cover.valid() || stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  const auto path=flowCoverFile(book),tmp=path+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"BOOKS-FLOW1 %llu %llu\n",
      static_cast<unsigned long long>(st.st_size),static_cast<unsigned long long>(st.st_mtime))>0;
  if(ok)ok=std::fputc(cover.width,f)!=EOF && std::fputc(cover.height,f)!=EOF;
  for(auto p:cover.pixels) {
    if(!ok)break;
    ok=std::fputc(p&255,f)!=EOF && std::fputc(p>>8,f)!=EOF;
  }
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(tmp.c_str(),path.c_str())==0)return true;
  std::remove(tmp.c_str());return false;
}
inline bool loadFlowCover(const std::string& book,FlowCover& out) {
  struct stat st{};
  if(stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  FILE* f=std::fopen(flowCoverFile(book).c_str(),"rb");
  if(!f)return false;
  char header[128]{};
  unsigned long long bytes=0,changed=0;
  bool ok=std::fgets(header,sizeof(header),f) &&
      parseShelfIdentityHeader(header,"BOOKS-FLOW1",bytes,changed) &&
      bytes==static_cast<unsigned long long>(st.st_size) &&
      changed==static_cast<unsigned long long>(st.st_mtime);
  FlowCover next;
  if(ok) {
    int w=std::fgetc(f),h=std::fgetc(f);
    ok=w>0 && h>0 && unsigned(w)<=FlowCover::maxWidth && unsigned(h)<=FlowCover::maxHeight;
    if(ok) {
      next.width=unsigned(w);next.height=unsigned(h);
      next.pixels.resize(size_t(w)*h);
      for(auto& p:next.pixels) {
        int lo=std::fgetc(f),hi=std::fgetc(f);
        if(lo==EOF || hi==EOF){ok=false;break;}
        p=uint16_t(lo|(hi<<8));
      }
      if(ok)ok=std::fgetc(f)==EOF && !std::ferror(f);
    }
  }
  std::fclose(f);
  if(ok)out=std::move(next);
  return ok;
}
} // namespace books
