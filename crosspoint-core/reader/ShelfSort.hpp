// SPDX-License-Identifier: MIT
#pragma once
#include <algorithm>
#include "ShelfAlphabet.hpp"
#include <numeric>
#include <utility>
#include <cstddef>
#include <string>
#include <vector>

namespace books {
// Folders: the card's folders, books A-Z in each (ShelfFolders.hpp).
enum class ShelfSortMode { Title, Recent, RecentlyAdded, Folders };
inline const char* shelfSortLabel(ShelfSortMode sort) {
  switch(sort) {
    case ShelfSortMode::Title:return "Title A-Z";
    case ShelfSortMode::Recent:return "Recently read";
    case ShelfSortMode::RecentlyAdded:return "Recently added";
    case ShelfSortMode::Folders:return "Folders";
  }
  return "Title A-Z";
}
inline int compareShelfNames(const std::string& a,const std::string& b) {
  return alphabet::sortKey(a).compare(alphabet::sortKey(b));
}
template<class Entry>
inline void sortShelfBooks(std::vector<Entry>& books,ShelfSortMode mode,
                          const std::vector<std::string>& recent) {
  std::vector<std::string> keys;
  std::vector<size_t> order(books.size()),ranks(books.size(),recent.size());
  keys.reserve(books.size());
  std::iota(order.begin(),order.end(),0);
  for(size_t i=0;i<books.size();++i) {
    keys.push_back(alphabet::sortKey(books[i].label));
    if(mode==ShelfSortMode::Recent)
      ranks[i]=std::find(recent.begin(),recent.end(),books[i].path)-recent.begin();
  }
  std::sort(order.begin(),order.end(),[&](size_t lhs,size_t rhs) {
    const auto& a=books[lhs];
    const auto& b=books[rhs];
    if(mode==ShelfSortMode::RecentlyAdded && a.modified!=b.modified)
      return a.modified>b.modified;
    if(ranks[lhs]!=ranks[rhs])return ranks[lhs]<ranks[rhs];
    return keys[lhs]!=keys[rhs]?keys[lhs]<keys[rhs]:a.path<b.path;
  });
  std::vector<Entry> sorted;
  sorted.reserve(books.size());
  for(const auto i:order)sorted.push_back(std::move(books[i]));
  books=std::move(sorted);
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
