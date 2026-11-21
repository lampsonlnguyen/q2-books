// SPDX-License-Identifier: MIT
#pragma once
// Persistent CrossPoint visible-codepoint ranges. Identity/version gated;
// bounded binary records, atomic replacement, no write on page turns.
#include "ReadingPosition.hpp"
#include "ShelfIdentity.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace crosspod {
struct Highlight {
  uint32_t spine=0;
  uint32_t begin=0;  // inclusive, original CrossPoint visible-text codepoint offset
  uint32_t end=0;    // exclusive
};
constexpr size_t kMaxHighlights=128;
inline bool validHighlight(const Highlight& h) {
  return h.spine<100000 && h.begin<h.end && h.end<=100000000;
}
inline std::string highlightsFile(const std::string& book) {
  return positionFile(book)+".highlights";
}
inline bool highlightBookIdentity(const std::string& path,
                                  unsigned long long& bytes,
                                  unsigned long long& mtime) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  bytes=static_cast<unsigned long long>(st.st_size);
  mtime=static_cast<unsigned long long>(st.st_mtime);
  return true;
}
inline std::vector<Highlight> loadHighlights(const std::string& book) {
  std::vector<Highlight> out;
  unsigned long long bytes=0,mtime=0;
  if(!highlightBookIdentity(book,bytes,mtime))return out;
  FILE* f=std::fopen(highlightsFile(book).c_str(),"rb");
  if(!f)return out;
  char header[128]{};
  unsigned long long storedBytes=0,storedMtime=0;
  const bool headerOk=std::fgets(header,sizeof(header),f) &&
    parseShelfIdentityHeader(header,"CROSSPOD-HIGHLIGHTS1",
                             storedBytes,storedMtime) &&
    storedBytes==bytes && storedMtime==mtime;
  uint8_t count=0;
  if(!headerOk || std::fread(&count,1,1,f)!=1 || count>kMaxHighlights) {
    std::fclose(f);return {};
  }
  out.reserve(count);
  for(unsigned i=0;i<count;++i) {
    uint8_t data[12]{};
    if(std::fread(data,1,sizeof(data),f)!=sizeof(data)) {
      out.clear();break;
    }
    const auto value=[&](int start)->uint32_t {
      return static_cast<uint32_t>(data[start]) |
        (static_cast<uint32_t>(data[start+1])<<8) |
        (static_cast<uint32_t>(data[start+2])<<16) |
        (static_cast<uint32_t>(data[start+3])<<24);
    };
    Highlight h{value(0),value(4),value(8)};
    if(!validHighlight(h)) {out.clear();break;}
    out.push_back(h);
  }
  if(std::fgetc(f)!=EOF)out.clear(); // reject truncated/extended malformed files
  std::fclose(f);
  return out;
}
inline bool saveHighlights(const std::string& book,
                           const std::vector<Highlight>& items) {
  if(items.size()>kMaxHighlights)return false;
  unsigned long long bytes=0,mtime=0;
  if(!highlightBookIdentity(book,bytes,mtime))return false;
  const std::string file=highlightsFile(book),tmp=file+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"CROSSPOD-HIGHLIGHTS1 %llu %llu\n",
                        bytes,mtime)>0;
  const uint8_t count=static_cast<uint8_t>(items.size());
  if(ok && std::fwrite(&count,1,1,f)!=1)ok=false;
  for(const auto& h:items) {
    if(!validHighlight(h)){ok=false;break;}
    uint8_t data[12]{};
    const uint32_t values[]={h.spine,h.begin,h.end};
    for(unsigned i=0;i<3;++i)
      for(unsigned b=0;b<4;++b)
        data[i*4+b]=static_cast<uint8_t>((values[i]>>(b*8))&0xff);
    if(std::fwrite(data,1,sizeof(data),f)!=sizeof(data)){ok=false;break;}
  }
  if(std::fflush(f)!=0)ok=false;
  if(ok && ::fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(tmp.c_str(),file.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
}
inline bool addHighlight(const std::string& book,Highlight entry) {
  if(!validHighlight(entry))return false;
  auto items=loadHighlights(book);
  // Merge intersecting/touching highlights in one spine, keeping a bounded
  // ordered set and preventing duplicate writes.
  for(auto it=items.begin();it!=items.end();) {
    if(it->spine==entry.spine && it->begin<=entry.end && entry.begin<=it->end) {
      entry.begin=std::min(entry.begin,it->begin);
      entry.end=std::max(entry.end,it->end);
      it=items.erase(it);
    } else ++it;
  }
  if(items.size()>=kMaxHighlights)return false;
  items.push_back(entry);
  std::sort(items.begin(),items.end(),[](const Highlight& a,const Highlight& b){
    if(a.spine!=b.spine)return a.spine<b.spine;
    return a.begin<b.begin;
  });
  return saveHighlights(book,items);
}
inline bool removeHighlightAt(const std::string& book,uint32_t spine,uint32_t offset) {
  auto items=loadHighlights(book);
  const auto old=items.size();
  items.erase(std::remove_if(items.begin(),items.end(),[&](const Highlight& h){
    return h.spine==spine && h.begin<=offset && offset<h.end;
  }),items.end());
  return items.size()!=old && saveHighlights(book,items);
}
} // namespace crosspod
