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
  for(unsigned y=0;y<next.height;++y)for(unsigned x=0;x<next.width;++x) {
    const unsigned left=x*w/next.width,right=(x+1)*w/next.width;
    const unsigned top=y*h/next.height,bottom=(y+1)*h/next.height;
    unsigned r=0,g=0,b=0;
    for(unsigned sy=top;sy<bottom;++sy)for(unsigned sx=left;sx<right;++sx) {
      const auto* p=rgb+3*(size_t(sy)*w+sx);
      r+=p[0];g+=p[1];b+=p[2];
    }
    const unsigned count=(right-left)*(bottom-top);
    next.pixels[size_t(y)*next.width+x]=
        uint16_t((r/count&248u)<<8 | (g/count&252u)<<3 | b/count>>3);
  }
  out=std::move(next);
  return true;
}
// Shared bounded RGB565 payload used by local EPUB and OPDS caches.
inline bool writeCoverPixels(FILE* file,const FlowCover& cover) {
  if(!cover.valid() || std::fputc(cover.width,file)==EOF || std::fputc(cover.height,file)==EOF)return false;
  for(auto pixel:cover.pixels)
    if(std::fputc(pixel&255,file)==EOF || std::fputc(pixel>>8,file)==EOF)return false;
  return true;
}
inline bool readCoverPixels(FILE* file,FlowCover& cover) {
  const int w=std::fgetc(file),h=std::fgetc(file);
  if(w<=0 || h<=0 || unsigned(w)>FlowCover::maxWidth || unsigned(h)>FlowCover::maxHeight)return false;
  FlowCover next{unsigned(w),unsigned(h),std::vector<uint16_t>(size_t(w)*h)};
  for(auto& pixel:next.pixels) {
    const int low=std::fgetc(file),high=std::fgetc(file);
    if(low==EOF || high==EOF)return false;
    pixel=uint16_t(low|(high<<8));
  }
  if(std::fgetc(file)!=EOF || std::ferror(file))return false;
  cover=std::move(next);
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
  ok=ok && writeCoverPixels(f,cover);
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
  ok=ok && readCoverPixels(f,out);
  std::fclose(f);
  return ok;
}
} // namespace books
