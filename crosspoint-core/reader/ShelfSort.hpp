// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <string>
#include <vector>

namespace books {
enum class ShelfSortMode { Title, Recent, RecentlyAdded };
inline const char* shelfSortLabel(ShelfSortMode sort) {
  switch(sort) {
    case ShelfSortMode::Title:return "Title A-Z";
    case ShelfSortMode::Recent:return "Recently read";
    case ShelfSortMode::RecentlyAdded:return "Recently added";
  }
  return "Title A-Z";
}
inline int compareShelfNames(const std::string& a,const std::string& b) {
  const size_t n=std::min(a.size(),b.size());
  for(size_t i=0;i<n;++i) {
    const auto x=static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(a[i])));
    const auto y=static_cast<unsigned char>(std::tolower(static_cast<unsigned char>(b[i])));
    if(x<y)return -1;
    if(x>y)return 1;
  }
  return a.size()<b.size()?-1:(a.size()>b.size()?1:0);
}
template<class Entry>
inline void sortShelfBooks(std::vector<Entry>& books,ShelfSortMode mode,
                          const std::vector<std::string>& recent) {
  const auto rank=[&](const std::string& path) {
    const auto it=std::find(recent.begin(),recent.end(),path);
    return it==recent.end()?recent.size():static_cast<size_t>(it-recent.begin());
  };
  std::sort(books.begin(),books.end(),[&](const Entry& a,const Entry& b) {
    if(mode==ShelfSortMode::RecentlyAdded && a.modified!=b.modified)
      return a.modified>b.modified;
    if(mode==ShelfSortMode::Recent) {
      const auto r1=rank(a.path),r2=rank(b.path);
      if(r1!=r2)return r1<r2;
    }
    const int named=compareShelfNames(a.label,b.label);
    return named?named<0:a.path<b.path;
  });
}
template<class Entry>
inline size_t lastOpenedIndex(const std::vector<Entry>& books,
                              const std::vector<std::string>& recent) {
  for(const auto& path:recent)
    for(size_t i=0;i<books.size();++i)
      if(books[i].path==path)return i;
  return 0; // no history: retain alphabetically-selected fallback
}
} // namespace books
