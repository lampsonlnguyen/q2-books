// SPDX-License-Identifier: MIT
#include "ShelfHistory.hpp"
#include "ShelfSort.hpp"
#include "ShelfDetails.hpp"
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

struct TestBook {
  std::string path;
  std::string label;
  long modified;
};
int main() {
  char dir[]="/tmp/crosspod-shelf-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("CROSSPOD_STATE_DIR",dir,1)==0);
  const std::string a=std::string(dir)+"/A story.epub";
  const std::string b=std::string(dir)+"/B story.epub";
  for(const auto& path:{a,b}) {
    FILE* f=std::fopen(path.c_str(),"wb");
    assert(f);
    assert(std::fputs("book",f)>=0);
    assert(std::fclose(f)==0);
  }
  assert(crosspod::loadRecentBooks().empty());
  assert(crosspod::recordRecentBook(a));
  assert(crosspod::recordRecentBook(b));
  assert(crosspod::recordRecentBook(a));
  auto recent=crosspod::loadRecentBooks();
  assert(recent.size()==2 && recent[0]==a && recent[1]==b);
  std::vector<TestBook> books={{b,"zebra",30},{a,"Alpha",20}};
  crosspod::sortShelfBooks(books,crosspod::ShelfSortMode::Title,recent);
  assert(books[0].path==a);
  crosspod::sortShelfBooks(books,crosspod::ShelfSortMode::RecentlyAdded,recent);
  assert(books[0].path==b);
  crosspod::sortShelfBooks(books,crosspod::ShelfSortMode::Recent,recent);
  assert(books[0].path==a);
  assert(crosspod::lastOpenedIndex(books,recent)==0);
  assert(crosspod::saveShelfDetails(a,{u8"Tiếng Việt",u8"Nguyễn Văn A"}));
  crosspod::ShelfDetails details;
  assert(crosspod::loadShelfDetails(a,details));
  assert(details.title==u8"Tiếng Việt" && details.author==u8"Nguyễn Văn A");
  // Overwriting an EPUB must invalidate cached title/author metadata.
  FILE* f=std::fopen(a.c_str(),"ab");
  assert(f);
  assert(std::fputs("changed",f)>=0);
  assert(std::fclose(f)==0);
  assert(!crosspod::loadShelfDetails(a,details));
  // Ignore malformed history rather than trusting arbitrary SD bytes.
  f=std::fopen(crosspod::recentBooksFile().c_str(),"wb");
  assert(f);
  assert(std::fputs("CROSSPOD-RECENT1\nnot-hex\n",f)>=0);
  assert(std::fclose(f)==0);
  assert(crosspod::loadRecentBooks().empty());
  assert(std::remove(a.c_str())==0);
  assert(std::remove(b.c_str())==0);
  return 0;
}
