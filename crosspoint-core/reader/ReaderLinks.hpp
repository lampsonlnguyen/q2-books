// SPDX-License-Identifier: MIT
#pragma once
// Internal EPUB links are not web URLs. Resolve against the current OPF
// manifest item, reject archive escape and keep the fragment intact.
#include <cctype>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace crosspod {
struct InternalLink {std::string path;std::string anchor;bool valid=false;};
inline InternalLink resolveInternalLink(std::string_view chapter,
                                        std::string_view href) {
  InternalLink result{};
  if(href.empty() || href.size()>255 || chapter.empty() ||
     href.front()=='/' || href.front()=='\\' ||
     href.find('?')!=std::string_view::npos ||
     href.find('\\')!=std::string_view::npos ||
     href.find(':')!=std::string_view::npos)return result;
  const size_t hash=href.find('#');
  if(hash!=std::string_view::npos) {
    result.anchor=std::string(href.substr(hash+1));
    href=href.substr(0,hash);
  }
  if(result.anchor.size()>180)return {};
  const auto slash=chapter.rfind('/');
  std::string location=href.empty()?std::string(chapter):
      (slash==std::string_view::npos?std::string(href):
       std::string(chapter.substr(0,slash+1))+std::string(href));
  std::vector<std::string> chunks;
  for(size_t i=0;i<location.size();) {
    const auto end=location.find('/',i);
    const auto part=location.substr(i,end==std::string::npos?
                                      std::string::npos:end-i);
    if(part=="..") {
      if(chunks.empty())return {};
      chunks.pop_back();
    } else if(!part.empty() && part!=".") {
      if(part.find('%')!=std::string::npos) {
        // Percent-encoded filenames need the same decoding as upstream
        // FsHelpers. Don't guess and jump to the wrong spine.
        return {};
      }
      chunks.push_back(part);
    }
    if(end==std::string::npos)break;
    i=end+1;
  }
  if(chunks.empty())return {};
  for(const auto& part:chunks) {
    if(!result.path.empty())result.path+='/';
    result.path+=part;
  }
  result.valid=true;
  return result;
}
} // namespace crosspod
