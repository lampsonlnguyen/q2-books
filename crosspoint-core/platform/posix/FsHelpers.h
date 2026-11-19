#pragma once
#include <string>
#include <string_view>
#include <vector>
namespace FsHelpers {

inline int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c-'0';
  if (c >= 'a' && c <= 'f') return 10+c-'a';
  if (c >= 'A' && c <= 'F') return 10+c-'A';
  return -1;
}
inline std::string decodeUriEscapes(const std::string& path) {
  std::string result;
  result.reserve(path.size());
  for (size_t i=0;i<path.size();i++) {
    if (path[i]=='%' && i+2<path.size()) {
      int high=hexDigit(path[i+1]), low=hexDigit(path[i+2]);
      if(high>=0 && low>=0) {
        result.push_back(static_cast<char>(high*16+low));
        i+=2;
        continue;
      }
    }
    result.push_back(path[i]);
  }
  return result;
}
inline std::string normalisePath(const std::string& path) {
  std::vector<std::string_view> parts;
  parts.reserve(8);
  size_t begin=0;
  for(size_t i=0;i<=path.size();i++) {
    if (i!=path.size() && path[i]!='/')continue;
    if(i>begin) {
      std::string_view part(path.data()+begin,i-begin);
      if(part=="..") { if(!parts.empty())parts.pop_back(); }
      else if(part!=".")parts.push_back(part);
    }
    begin=i+1;
  }
  std::string out;
  for(auto part:parts) {
    if(!out.empty())out+='/';
    out.append(part.data(),part.size());
  }
  return out;
}

inline bool hasTxtExtension(std::string_view p) {
  if (p.size()<4) return false;
  p.remove_prefix(p.size()-4);
  return (p==".txt" || p==".TXT");
}
inline bool hasMarkdownExtension(std::string_view p) {
  if(p.size()<3)return false;
  std::string_view e=p.substr(p.rfind('.')==std::string_view::npos?p.size():p.rfind('.'));
  return e==".md"||e==".MD"||e==".markdown"||e==".MARKDOWN";
}
inline std::string getFileNameWithoutExtension(std::string_view p) {
  auto slash=p.find_last_of("/\\");
  if(slash!=std::string_view::npos)p.remove_prefix(slash+1);
  auto dot=p.rfind('.');
  if(dot!=std::string_view::npos)p=p.substr(0,dot);
  return std::string(p);
}
}
