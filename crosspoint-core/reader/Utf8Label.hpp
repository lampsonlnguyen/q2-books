// SPDX-License-Identifier: MIT
#pragma once
#include <Utf8.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>

// Normalize NFD titles (including Vietnamese/macOS filenames) before
// truncation; preserve complete UTF-8 characters and trailing combining marks.
namespace crosspod {
inline std::string clippedUiLabel(const std::string& raw,size_t limit) {
  const bool unicode=std::any_of(raw.begin(),raw.end(),[](unsigned char c){
    return c>=0x80;
  });
  const std::string normalized=unicode?utf8ComposeNfc(raw):raw;
  std::string output;
  const unsigned char* cur=
      reinterpret_cast<const unsigned char*>(normalized.c_str());
  size_t visible=0;
  while(*cur) {
    const unsigned char* before=cur;
    const uint32_t cp=utf8NextCodepoint(&cur);
    if(cur<=before)break;
    const bool mark=utf8IsCombiningMark(cp) || isHebrewMark(cp) ||
                    isArabicVowelMark(cp);
    if(visible>=limit && !mark) {
      cur=before;
      break;
    }
    if(cp==REPLACEMENT_GLYPH)
      utf8AppendCodepoint(REPLACEMENT_GLYPH,output);
    else
      output.append(reinterpret_cast<const char*>(before),
                    static_cast<size_t>(cur-before));
    if(!mark)++visible;
  }
  if(*cur)output+="...";
  return output;
}
} // namespace crosspod
