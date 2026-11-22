// SPDX-License-Identifier: MIT
#pragma once
// Offline dictionary: optional user-supplied UTF-8 tab-separated word/definition
// file in the app state dir. No network, no paid lookup, no firmware changes.
#include "ReadingPosition.hpp"
#include "StarDict.hpp"
#include "UnicodeCase.hpp"
#include <ctime>
#include <sys/stat.h>
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
      if(utf8Lower(normalizedDictionaryWord(entry.substr(0,tab)))==utf8Lower(needle)) {
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
// Dictionaries the reader turned off (Reading settings -> Dictionaries):
// one name per line in <state>/dictionaries-off.conf.
inline std::string dictionaryChoicesFile() {return dictionaryStateDir()+"/dictionaries-off.conf";}
inline std::vector<std::string> disabledDictionaries() {
  std::vector<std::string> off;
  if(FILE* f=std::fopen(dictionaryChoicesFile().c_str(),"rb")) {
    char line[256];
    for(int i=0;i<64 && std::fgets(line,sizeof line,f);++i) {
      std::string name(line);
      while(!name.empty()&&(name.back()=='\n'||name.back()=='\r'))name.pop_back();
      if(!name.empty())off.push_back(name);
    }
    std::fclose(f);
  }
  return off;
}
inline bool saveDisabledDictionaries(const std::vector<std::string>& off) {
  const std::string file=dictionaryChoicesFile(),tmp=file+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=true;
  for(const auto& name:off)ok=ok&&std::fprintf(f,"%s\n",name.c_str())>0;
  ok=std::fclose(f)==0&&ok;
  if(ok&&std::rename(tmp.c_str(),file.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
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
// The spellings a lookup tries, in order: as selected (punctuation off),
// all lower case, first letter capital, every word capital. StarDict
// compares ASCII case-insensitively only, so "Đà Lạt" and "đà lạt" are
// different keys there.
inline std::vector<std::string> lookupSpellings(const std::string& raw) {
  std::string word=raw;
  while(!word.empty() && std::string(".,!?;:)]}\"'").find(word.back())!=std::string::npos)word.pop_back();
  while(!word.empty() && std::string("([{\"'").find(word.front())!=std::string::npos)word.erase(word.begin());
  std::vector<std::string> out;
  for(std::string v:{word,utf8Lower(word),utf8CapitalizeFirst(utf8Lower(word)),utf8TitleCase(utf8Lower(word))})
    if(!v.empty() && v.size()<=stardict::kMaxWordBytes &&
       std::find(out.begin(),out.end(),v)==out.end())out.push_back(v);
  return out;
}
// StarDict matches first (each enabled dictionary in path order), then the
// TSV. A phrase with no match is tried again by its first word.
inline std::vector<DictionaryMatch> lookupDefinitions(const std::string& raw,bool phraseFallback=true) {
  std::vector<DictionaryMatch> out;
  const auto spellings=lookupSpellings(raw);
  if(spellings.empty())return out;
  const auto off=disabledDictionaries();
  size_t total=0;
  for(auto& d:openStardicts()) {
    if(std::find(off.begin(),off.end(),d->name)!=off.end())continue;
    for(const auto& spelling:spellings) {
      for(auto& [headword,text]:d->lookup(spelling)) {
        if(total>=stardict::kMaxDefinitionText*2)break;
        const bool seen=std::any_of(out.begin(),out.end(),[&](const DictionaryMatch& m) {
          return m.source==d->name && m.headword==headword && m.text.compare(0,64,text,0,64)==0;});
        if(seen)continue;
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
  }
  if(std::find(off.begin(),off.end(),"dictionary.tsv")==off.end()) {
    const std::string tsv=lookupDefinition(raw);
    if(!tsv.empty())out.push_back({"dictionary.tsv",normalizedDictionaryWord(raw),tsv});
  }
  if(out.empty() && phraseFallback) {
    const auto space=spellings.front().find(' ');
    if(space!=std::string::npos && space>0)
      return lookupDefinitions(spellings.front().substr(0,space),false);
  }
  return out;
}
// The installed dictionaries, by name, for the Dictionaries list (the TSV last).
inline std::vector<std::string> dictionaryNames() {
  std::vector<std::string> names;
  for(auto& d:openStardicts())names.push_back(d->name);
  struct stat st{};
  if(::stat(dictionaryFile().c_str(),&st)==0)names.push_back("dictionary.tsv");
  return names;
}
// DI-04 vocabulary log: <state>/vocabulary.tsv, one looked-up word a line:
// local time, the word as selected, the first headword found, the book's
// title. Capped at 1 MB (older lines stay; new ones stop being added).
inline std::string vocabularyFile() {return dictionaryStateDir()+"/vocabulary.tsv";}
inline bool logVocabulary(const std::string& word,const std::string& headword,
                          const std::string& book,std::time_t when=std::time(nullptr)) {
  const auto clean=[](std::string s) {
    for(char& c:s)if(c=='\t'||c=='\n'||c=='\r')c=' ';
    if(s.size()>200)s.resize(200);
    return s;
  };
  struct stat st{};
  if(::stat(vocabularyFile().c_str(),&st)==0 && st.st_size>(1<<20))return false;
  FILE* f=std::fopen(vocabularyFile().c_str(),"ab");
  if(!f)return false;
  char stamp[32]="";
  std::tm local{};
  if(when>946684800 && localtime_r(&when,&local))  // a clock before 2000 is not set
    std::strftime(stamp,sizeof stamp,"%Y-%m-%d %H:%M",&local);
  const bool ok=std::fprintf(f,"%s\t%s\t%s\t%s\n",stamp,clean(word).c_str(),
                             clean(headword).c_str(),clean(book).c_str())>0;
  return std::fclose(f)==0 && ok;
}
} // namespace books
