// SPDX-License-Identifier: MIT
#pragma once
// Per-book cached title/author from the original CrossPoint OPF metadata.
// Book identity invalidates stale metadata when an EPUB is replaced.
#include "ReadingPosition.hpp"
#include "ShelfIdentity.hpp"
#include "ShelfHistory.hpp"
#include <cstdio>
#include <cstdlib>
#include <cerrno>
#include <cstring>
#include <utility>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace crosspod {
struct ShelfDetails { std::string title,author; };
inline std::string shelfDetailsFile(const std::string& path) {
  return positionFile(path)+".shelf";
}
inline bool loadShelfDetails(const std::string& path,ShelfDetails& details) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  FILE* f=std::fopen(shelfDetailsFile(path).c_str(),"rb");
  if(!f)return false;
  char header[128]{},title[2048]{},author[2048]{};
  const bool read=std::fgets(header,sizeof(header),f) &&
                  std::fgets(title,sizeof(title),f) &&
                  std::fgets(author,sizeof(author),f);
  std::fclose(f);
  if(!read)return false;
  unsigned long long bytes=0,modified=0;
  if(!parseShelfIdentityHeader(header,"CROSSPOD-SHELF1",bytes,modified) ||
     bytes!=static_cast<unsigned long long>(st.st_size) ||
     modified!=static_cast<unsigned long long>(st.st_mtime))return false;
  // Decode hexadecimal UTF-8 metadata without applying path rules.
  const auto decodeText=[](const char* data,std::string& out)->bool {
    const std::string encoded(data);
    if(encoded.empty()||encoded.back()!='\n')return false;
    const size_t len=encoded.size()-1;
    if(len>2000 || len%2)return false;
    std::string result;
    result.reserve(len/2);
    const auto digit=[](char c)->int {
      if(c>='0'&&c<='9')return c-'0';
      if(c>='a'&&c<='f')return c-'a'+10;
      return -1;
    };
    for(size_t i=0;i<len;i+=2) {
      const int a=digit(encoded[i]),b=digit(encoded[i+1]);
      if(a<0||b<0)return false;
      const char ch=static_cast<char>(a*16+b);
      if(ch=='\0'||ch=='\n'||ch=='\r')return false;
      result+=ch;
    }
    out=std::move(result);
    return true;
  };
  ShelfDetails found;
  if(!decodeText(title,found.title)||!decodeText(author,found.author))return false;
  details=std::move(found);
  return true;
}
inline bool saveShelfDetails(const std::string& path,const ShelfDetails& details) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
  if(details.title.size()>1000||details.author.size()>1000)return false;
  const std::string file=shelfDetailsFile(path),temp=file+".tmp";
  FILE* f=std::fopen(temp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"CROSSPOD-SHELF1 %llu %llu\n",
    static_cast<unsigned long long>(st.st_size),
    static_cast<unsigned long long>(st.st_mtime))>0;
  if(ok && std::fprintf(f,"%s\n",hexBookPath(details.title).c_str())<0)ok=false;
  if(ok && std::fprintf(f,"%s\n",hexBookPath(details.author).c_str())<0)ok=false;
  if(std::fflush(f)!=0)ok=false;
  if(ok&&fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok&&std::rename(temp.c_str(),file.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace crosspod
