// SPDX-License-Identifier: MIT
#include "ShelfHistory.hpp"
#include "ShelfSort.hpp"
#include "ShelfDetails.hpp"
#include "ShelfCover.hpp"
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
  unsigned long long bytes=0,modified=0;
  assert(crosspod::parseShelfIdentityHeader(
      "CROSSPOD-SHELF1 4 6\n","CROSSPOD-SHELF1",bytes,modified));
  assert(bytes==4 && modified==6);
  assert(!crosspod::parseShelfIdentityHeader(
      "CROSSPOD-SHELF1 184467440737095516160 6\n",
      "CROSSPOD-SHELF1",bytes,modified));
  assert(!crosspod::parseShelfIdentityHeader(
      "CROSSPOD-SHELF1 -1 2\n","CROSSPOD-SHELF1",bytes,modified));
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
  crosspod::PngPreview picture;
  picture.width=2;
  picture.height=2;
  picture.gray={0,255,255,0};
  crosspod::ShelfCover cover,loadedCover;
  assert(crosspod::makeShelfCover(picture,cover));
  assert(crosspod::saveShelfCover(a,cover));
  assert(crosspod::loadShelfCover(a,loadedCover));
  assert(cover.ink==loadedCover.ink);
  // Overwriting an EPUB must invalidate cached title/author metadata.
  FILE* f=std::fopen(a.c_str(),"ab");
  assert(f);
  assert(std::fputs("changed",f)>=0);
  assert(std::fclose(f)==0);
  assert(!crosspod::loadShelfDetails(a,details));
  assert(!crosspod::loadShelfCover(a,loadedCover));
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
