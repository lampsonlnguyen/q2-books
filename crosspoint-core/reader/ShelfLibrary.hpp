// SPDX-License-Identifier: MIT
#pragma once
// Bounded recursive /Books discovery. Never traverse symlinked directories
// on removable media, and never load EPUB contents to enumerate the library.
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <ctime>
#include <cstdint>
#include <dirent.h>
#include <string>
#include <string_view>
#include <sys/stat.h>
#include <utility>
#include <vector>

namespace books {
struct ShelfEntry {
  std::string path;
  std::string label;
  std::time_t modified=0;
  uint64_t bytes=0;
};

struct ShelfScanLimits {
  // M4: a large library. 4000 entries are about 1 MB of RAM; their titles
  // and covers stay on the card until a row is drawn.
  size_t maxBooks=4000;
  size_t maxFolders=1024;
  size_t maxDepth=5;  // Books/<dir1>/.../<dir5>/
};
inline bool isEpubFilename(std::string_view name) {
  if(name.size()<6)return false;
  name.remove_prefix(name.size()-5);
  constexpr std::string_view suffix=".epub";
  for(size_t i=0;i<suffix.size();++i) {
    const unsigned char ch=static_cast<unsigned char>(name[i]);
    if(static_cast<char>(std::tolower(ch))!=suffix[i])return false;
  }
  return true;
}
// RD-04: plain-text books (see TxtPackage.hpp) sit on the shelf beside EPUBs.
inline bool isTxtFilename(std::string_view name) {
  if(name.size()<5)return false;
  name.remove_prefix(name.size()-4);
  constexpr std::string_view suffix=".txt";
  for(size_t i=0;i<suffix.size();++i) {
    const unsigned char ch=static_cast<unsigned char>(name[i]);
    if(static_cast<char>(std::tolower(ch))!=suffix[i])return false;
  }
  return true;
}
inline bool isBookFilename(std::string_view name) {
  return isEpubFilename(name) || isTxtFilename(name);
}
inline std::vector<ShelfEntry> scanCardBooks(
    const std::string& root,const ShelfScanLimits limits={}) {
  std::vector<ShelfEntry> books;
  if(root.empty() || root.size()>1000 || !limits.maxBooks ||
     !limits.maxFolders)return books;
  struct Folder {std::string path;size_t depth;};
  std::vector<Folder> pending{{root,0}};
  size_t discoveredFolders=0;
  // Do not follow symbolic links (including root) or special files.
  struct stat rootInfo{};
  if(lstat(root.c_str(),&rootInfo)!=0 || !S_ISDIR(rootInfo.st_mode))
    return books;
  for(size_t next=0;next<pending.size() && books.size()<limits.maxBooks;++next) {
    const Folder& folder=pending[next];
    DIR* dir=opendir(folder.path.c_str());
    if(!dir)continue;
    std::vector<Folder> nested;
    for(dirent* entry;(entry=readdir(dir))!=nullptr;) {
      if(entry->d_name[0]=='.')continue;
      const std::string filename(entry->d_name);
      if(filename.size()>240)continue;
      const std::string path=folder.path+"/"+filename;
      if(path.size()>1000)continue;
      struct stat st{};
      if(lstat(path.c_str(),&st)!=0)continue;
      if(S_ISDIR(st.st_mode)) {
        if(folder.depth<limits.maxDepth &&
           discoveredFolders+nested.size()<limits.maxFolders)
          nested.push_back({path,folder.depth+1});
        continue;
      }
      if(!S_ISREG(st.st_mode) || !isBookFilename(filename) ||
         books.size()>=limits.maxBooks)continue;
      const size_t extension=isTxtFilename(filename)?4:5;  // ".txt" or ".epub"
      books.push_back({path,filename.substr(0,filename.size()-extension),st.st_mtime,static_cast<uint64_t>(st.st_size)});
    }
    closedir(dir);
    std::sort(nested.begin(),nested.end(),
      [](const Folder& a,const Folder& b){return a.path<b.path;});
    discoveredFolders+=nested.size();
    pending.insert(pending.end(),nested.begin(),nested.end());
  }
  std::sort(books.begin(),books.end(),
    [](const ShelfEntry& a,const ShelfEntry& b){
      const size_t count=std::min(a.label.size(),b.label.size());
      for(size_t i=0;i<count;++i) {
        const auto aa=static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(a.label[i])));
        const auto bb=static_cast<unsigned char>(
          std::tolower(static_cast<unsigned char>(b.label[i])));
        if(aa!=bb)return aa<bb;
      }
      return a.label.size()!=b.label.size()?
        a.label.size()<b.label.size():a.path<b.path;
    });
  return books;
}
} // namespace books
