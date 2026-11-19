#pragma once
#include <string>
#include <string_view>
namespace FsHelpers {
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
