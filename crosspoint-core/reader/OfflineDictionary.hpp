// SPDX-License-Identifier: MIT
#pragma once
// Offline dictionary: optional user-supplied UTF-8 tab-separated word/definition
// file in the app state dir. No network, no paid lookup, no firmware changes.
#include "ReadingPosition.hpp"
#include "StarDict.hpp"
#include <algorithm>
#include <memory>
#include <vector>
#include <cctype>
#include <cstdio>
#include <string>

namespace books {
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
  if(!f) {
    const auto file=dictionaryFile();
    f=std::fopen((file.substr(0,file.find_last_of('/'))+
                   "/dictionary-starter.tsv").c_str(),"rb");
  }
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
// One definition for the overlay: which dictionary, the headword as the
// dictionary spells it, and plain text.
struct DictionaryMatch {
  std::string source,headword,text;
};
// StarDict dictionaries live in <state>/dictionaries/ and in the card's
// Dictionaries/ folder (one folder per dictionary, or .ifo files directly).
inline std::string dictionaryStateDir() {
  const auto file=dictionaryFile();
  return file.substr(0,file.find_last_of('/'));
}
inline std::vector<std::string> stardictFolders() {
  return {dictionaryStateDir()+"/dictionaries","/mnt/mmc/Dictionaries"};
}
// Opened once per process; an .ifo that fails to open is reported once and
// left out. Each open dictionary holds only its sample table in memory.
inline std::vector<std::unique_ptr<stardict::Dictionary>>& openStardicts() {
  static std::vector<std::unique_ptr<stardict::Dictionary>> dictionaries;
  static bool scanned=false;
  if(scanned)return dictionaries;
  scanned=true;
  const std::string cache=dictionaryStateDir()+"/cache";
  for(const auto& folder:stardictFolders()) {
    for(const auto& ifo:stardict::findDictionaries(folder)) {
      if(dictionaries.size()>=16)break;
      auto d=std::make_unique<stardict::Dictionary>();
      if(d->open(ifo,cache))dictionaries.push_back(std::move(d));
      else std::fprintf(stderr,"Books: dictionary skipped (%s)\n",d->error.c_str());
    }
  }
  return dictionaries;
}
// StarDict matches first (each dictionary in path order), then the TSV.
inline std::vector<DictionaryMatch> lookupDefinitions(const std::string& raw) {
  std::vector<DictionaryMatch> out;
  const std::string needle=normalizedDictionaryWord(raw);
  if(needle.empty()||needle.size()>stardict::kMaxWordBytes)return out;
  size_t total=0;
  for(auto& d:openStardicts()) {
    for(auto& [headword,text]:d->lookup(needle)) {
      if(total>=stardict::kMaxDefinitionText*2)break;
      if(text.size()>stardict::kMaxDefinitionText) {
        size_t cut=stardict::kMaxDefinitionText;
        while(cut>0&&(static_cast<unsigned char>(text[cut])&0xC0)==0x80)--cut;
        text.resize(cut);
        text+="...";
      }
      total+=text.size();
      out.push_back({d->name,headword,std::move(text)});
    }
  }
  const std::string tsv=lookupDefinition(raw);
  if(!tsv.empty())out.push_back({"dictionary.tsv",needle,tsv});
  return out;
}
} // namespace books
