// SPDX-License-Identifier: MIT
// Contract between Q2 Pod's existing Books shelf and the independent
// CrossPoint-based Linux reader. The shelf supplies an absolute book path;
// CrossPoint owns all parsing/layout/page navigation after that handoff.
#pragma once
#include <string>
#include <string_view>
namespace crosspod {
enum class BookFormat { Unsupported, Txt, Epub };
struct BookLaunch {
  std::string path;
  BookFormat format{BookFormat::Unsupported};
  explicit operator bool() const { return format!=BookFormat::Unsupported; }
};
inline char lowercase_ascii(char c) {return c>='A'&&c<='Z'?char(c+32):c;}
inline bool suffix_icase(std::string_view s,std::string_view suffix){
  if(s.size()<suffix.size())return false;
  s.remove_prefix(s.size()-suffix.size());
  for(std::size_t i=0;i<s.size();i++)
    if(lowercase_ascii(s[i])!=suffix[i])return false;
  return true;
}
inline BookLaunch parse_q2_book(std::string_view path){
  // POSIX path, explicitly confined to Q2 Pod's existing Books directory.
  constexpr std::string_view base="/mnt/mmc/Books/";
  if(path.size()<=base.size()||path.substr(0,base.size())!=base||path.size()>1024)return {};
  auto rel=path.substr(base.size());
  if(rel.front()=='.'||rel.front()=='/'||rel.back()=='/')return {};
  auto pos=std::size_t{0};
  while(pos<rel.size()){
    auto end=rel.find('/',pos);
    if(end==std::string_view::npos)end=rel.size();
    auto seg=rel.substr(pos,end-pos);
    if(seg.empty()||seg=="."||seg=="..")return {};
    for(char c:seg)if(c=='\\'||c=='\0'||static_cast<unsigned char>(c)<0x20)return {};
    pos=end+1;
  }
  BookFormat format=BookFormat::Unsupported;
  if(suffix_icase(rel,".epub"))format=BookFormat::Epub;
  else if(suffix_icase(rel,".txt"))format=BookFormat::Txt;
  if(format==BookFormat::Unsupported)return {};
  return {std::string(path),format};
}
}
