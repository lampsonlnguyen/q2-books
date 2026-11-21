// SPDX-License-Identifier: MIT
#pragma once
// Bounded, atomic reading-history adapter for the Q2 CrossPod bookshelf.
// History contains paths only; the EPUB reader and its Section cache are unchanged.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

namespace crosspod {
constexpr size_t kMaxRecentBooks=20;
inline std::string recentBooksFile() {
  const char* state=std::getenv("CROSSPOD_STATE_DIR");
  return std::string(state&&*state?state:"/mnt/mmc/.crosspod")+"/recent-books.conf";
}
inline std::string hexBookPath(const std::string& path) {
  static constexpr char digits[]="0123456789abcdef";
  std::string encoded;
  encoded.reserve(path.size()*2);
  for(unsigned char ch:path) {
    encoded.push_back(digits[ch>>4]);
    encoded.push_back(digits[ch&15]);
  }
  return encoded;
}
inline bool parseHexBookPath(const std::string& encoded,std::string& path) {
  if(encoded.empty() || encoded.size()>2000 || encoded.size()%2)return false;
  const auto hexDigit=[](char ch)->int {
    if(ch>='0'&&ch<='9')return ch-'0';
    if(ch>='a'&&ch<='f')return ch-'a'+10;
    return -1;
  };
  std::string decoded;
  decoded.reserve(encoded.size()/2);
  for(size_t i=0;i<encoded.size();i+=2) {
    const int hi=hexDigit(encoded[i]),lo=hexDigit(encoded[i+1]);
    if(hi<0||lo<0)return false;
    const char ch=static_cast<char>(hi*16+lo);
    if(ch=='\0'||ch=='\n'||ch=='\r')return false;
    decoded.push_back(ch);
  }
  if(decoded.front()!='/')return false;
  path=std::move(decoded);
  return true;
}
inline std::vector<std::string> loadRecentBooks() {
  std::vector<std::string> items;
  FILE* f=std::fopen(recentBooksFile().c_str(),"rb");
  if(!f)return items;
  char line[2048]{};
  if(!std::fgets(line,sizeof(line),f) ||
     std::string(line)!="CROSSPOD-RECENT1\n") {
    std::fclose(f);
    return items;
  }
  for(size_t i=0;i<kMaxRecentBooks && std::fgets(line,sizeof(line),f);++i) {
    std::string encoded(line);
    if(encoded.empty() || encoded.back()!='\n') {items.clear();break;}
    encoded.pop_back();
    std::string path;
    if(!parseHexBookPath(encoded,path) ||
       std::find(items.begin(),items.end(),path)!=items.end()) {
      items.clear();break;
    }
    items.push_back(std::move(path));
  }
  std::fclose(f);
  return items;
}
inline bool saveRecentBooks(const std::vector<std::string>& items) {
  if(items.size()>kMaxRecentBooks)return false;
  const auto name=recentBooksFile(),tmp=name+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fputs("CROSSPOD-RECENT1\n",f)>=0;
  for(const auto& path:items) {
    std::string validated;
    if(path.size()>1000 || !parseHexBookPath(hexBookPath(path),validated) ||
       validated!=path) {ok=false;break;}
    if(std::fprintf(f,"%s\n",hexBookPath(path).c_str())<0) {ok=false;break;}
  }
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(tmp.c_str(),name.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
}
inline bool recordRecentBook(const std::string& path) {
  if(path.size()>1000 || path.empty() || path.front()!='/')return false;
  auto recent=loadRecentBooks();
  recent.erase(std::remove(recent.begin(),recent.end(),path),recent.end());
  recent.insert(recent.begin(),path);
  if(recent.size()>kMaxRecentBooks)recent.resize(kMaxRecentBooks);
  return saveRecentBooks(recent);
}
} // namespace crosspod
