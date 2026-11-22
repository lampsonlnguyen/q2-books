// SPDX-License-Identifier: MIT
#pragma once
// Lightweight EPUB-library filtering; book contents are never opened merely
// to search. NFC canonicalization preserves Vietnamese decomposed filenames.
#include <Utf8.h>
#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace books {
inline std::string foldShelfSearch(std::string_view raw) {
  std::string normalized=utf8ComposeNfc(std::string(raw));
  for(char& ch:normalized)
    if(static_cast<unsigned char>(ch)<0x80)
      ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return normalized;
}
inline bool shelfSearchMatches(const std::string& query,
                               const std::string& filename,
                               const std::string& title,
                               const std::string& author) {
  if(query.empty())return true;
  const auto needle=foldShelfSearch(query);
  if(needle.empty())return true;
  return foldShelfSearch(filename).find(needle)!=std::string::npos ||
         foldShelfSearch(title).find(needle)!=std::string::npos ||
         foldShelfSearch(author).find(needle)!=std::string::npos;
}
} // namespace books
