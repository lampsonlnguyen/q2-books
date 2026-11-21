// SPDX-License-Identifier: MIT
#pragma once
// Offline dictionary: optional user-supplied UTF-8 tab-separated word/definition
// file in the app state dir. No network, no paid lookup, no firmware changes.
#include "ReadingPosition.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <string>

namespace crosspod {
inline std::string dictionaryFile() {
  return positionFile("/__dictionary__").substr(
    0,positionFile("/__dictionary__").find_last_of('/'))+"/dictionary.tsv";
}
inline std::string normalizedDictionaryWord(std::string word) {
  while(!word.empty() && (word.back()=='.'||word.back()==','||
    word.back()=='!'||word.back()=='?'||word.back()==';'||word.back()==':'))
    word.pop_back();
  while(!word.empty() && (word.front()=='('||word.front()=='"'||
    word.front()=='\''))word.erase(word.begin());
  for(char& ch:word)
    if(static_cast<unsigned char>(ch)<128)
      ch=static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  return word;
}
inline std::string lookupDefinition(const std::string& raw) {
  const std::string needle=normalizedDictionaryWord(raw);
  if(needle.empty()||needle.size()>128)return {};
  // Cap both line length and lookups to keep removable-SD I/O bounded.
  FILE* f=std::fopen(dictionaryFile().c_str(),"rb");
  if(f) {
    char line[1024]{};
    for(unsigned i=0;i<50000 && std::fgets(line,sizeof(line),f);++i) {
      std::string entry(line);
      if(entry.empty() || entry.back()!='\n')continue;
      const auto tab=entry.find('\t');
      if(tab==std::string::npos)continue;
      if(normalizedDictionaryWord(entry.substr(0,tab))==needle) {
        std::fclose(f);
        std::string meaning=entry.substr(tab+1);
        while(!meaning.empty() && (meaning.back()=='\n'||meaning.back()=='\r'))
          meaning.pop_back();
        return meaning.substr(0,500);
      }
    }
    std::fclose(f);
  }
  return {};
}
} // namespace crosspod
