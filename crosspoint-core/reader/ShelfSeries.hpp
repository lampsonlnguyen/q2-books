// SPDX-License-Identifier: MIT
#pragma once
#include "ShelfSearch.hpp"
#include "UnicodeCase.hpp"
#include <cmath>
#include <cstdint>
#include <optional>
#include <limits>

namespace books {
inline std::string normalizeSeriesName(std::string_view text) {
  std::string name;
  name.reserve(std::min<size_t>(text.size(),512));
  bool space=false;
  for(size_t i=0;i<text.size();) {
    const unsigned char ch=text[i];
    if(ch==' ' || ch=='\t' || ch=='\r' || ch=='\n') {
      space=!name.empty();++i;continue;
    }
    const size_t bytes=ch<0x80?1:ch<0xe0?2:ch<0xf0?3:4;
    if(i+bytes>text.size() || name.size()+bytes+(space?1:0)>512)break;
    if(space)name.push_back(' ');
    name.append(text.substr(i,bytes));
    i+=bytes;space=false;
  }
  return utf8ComposeNfc(name);
}
inline std::string seriesIdentity(std::string_view name) {
  return utf8Lower(foldShelfSearch(normalizeSeriesName(name)));
}
inline constexpr float unknownSeriesVolume=std::numeric_limits<float>::infinity();
inline float compactSeriesVolume(std::optional<float> number) {
  return number && std::isfinite(*number) && *number>=0 && *number<=1000000.f?
      *number:unknownSeriesVolume;
}
// Eight bytes per indexed series book on both host and Q2.
struct SeriesVolumeRef {
  uint32_t index;
  float number;
};
static_assert(sizeof(SeriesVolumeRef)==8,"compact series references");
inline bool seriesVolumeBefore(const SeriesVolumeRef& a,const SeriesVolumeRef& b) {
  if(a.number!=b.number)return a.number<b.number;
  return a.index<b.index;
}
} // namespace books
