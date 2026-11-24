// SPDX-License-Identifier: MIT
#pragma once
#include "ReadingPosition.hpp"
#include "ShelfIdentity.hpp"
#include <algorithm>
namespace books {
inline bool saveShelfProgress(const std::string& book,uint32_t ppm) {
  struct stat st{};
  if(stat(book.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  const auto path=positionFile(book)+".percent",tmp=path+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"BOOKS-PERCENT1 %llu %llu\n%u\n",
      static_cast<unsigned long long>(st.st_size),static_cast<unsigned long long>(st.st_mtime),
      static_cast<unsigned>(std::min(ppm,1000000u)))>0;
  if(std::fflush(f)!=0)ok=false;
  if(ok&&fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok&&std::rename(tmp.c_str(),path.c_str())==0)return true;
  std::remove(tmp.c_str());return false;
}
inline bool loadShelfProgress(const std::string& book,uint32_t& ppm) {
  struct stat st{};
  if(stat(book.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  FILE* f=std::fopen((positionFile(book)+".percent").c_str(),"rb");
  if(!f)return false;
  char line[128]{},value[32]{};
  unsigned long long size=0,changed=0,n=0;
  bool ok=std::fgets(line,sizeof(line),f)&&
      parseShelfIdentityHeader(line,"BOOKS-PERCENT1",size,changed)&&
      size==static_cast<unsigned long long>(st.st_size)&&changed==static_cast<unsigned long long>(st.st_mtime)&&
      std::fgets(value,sizeof(value),f);
  const char* p=value;
  ok=ok&&parseIdentityNumber(p,n)&&*p=='\n'&&p[1]=='\0'&&n<=1000000&&std::fgetc(f)==EOF&&!std::ferror(f);
  std::fclose(f);
  if(ok)ppm=static_cast<uint32_t>(n);
  return ok;
}
} // namespace books
