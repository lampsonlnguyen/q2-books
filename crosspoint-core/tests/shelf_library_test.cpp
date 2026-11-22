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
  assert(books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 4 6\n","BOOKS-SHELF1",bytes,modified));
  assert(bytes==4 && modified==6);
  assert(!books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 184467440737095516160 6\n",
      "BOOKS-SHELF1",bytes,modified));
  assert(!books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 -1 2\n","BOOKS-SHELF1",bytes,modified));
  char dir[]="/tmp/books-shelf-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string a=std::string(dir)+"/A story.epub";
  const std::string b=std::string(dir)+"/B story.epub";
  for(const auto& path:{a,b}) {
    FILE* f=std::fopen(path.c_str(),"wb");
    assert(f);
    assert(std::fputs("book",f)>=0);
    assert(std::fclose(f)==0);
  }
  assert(books::loadRecentBooks().empty());
  assert(books::recordRecentBook(a));
  assert(books::recordRecentBook(b));
  assert(books::recordRecentBook(a));
  auto recent=books::loadRecentBooks();
  assert(recent.size()==2 && recent[0]==a && recent[1]==b);
  std::vector<TestBook> books={{b,"zebra",30},{a,"Alpha",20}};
  books::sortShelfBooks(books,books::ShelfSortMode::Title,recent);
  assert(books[0].path==a);
  books::sortShelfBooks(books,books::ShelfSortMode::RecentlyAdded,recent);
  assert(books[0].path==b);
  books::sortShelfBooks(books,books::ShelfSortMode::Recent,recent);
  assert(books[0].path==a);
  assert(books::lastOpenedIndex(books,recent)==0);
  assert(books::saveShelfDetails(a,{u8"Tiếng Việt",u8"Nguyễn Văn A"}));
  books::ShelfDetails details;
  assert(books::loadShelfDetails(a,details));
  assert(details.title==u8"Tiếng Việt" && details.author==u8"Nguyễn Văn A");
  books::PngPreview picture;
  picture.width=2;
  picture.height=2;
  picture.gray={0,255,255,0};
  books::ShelfCover cover,loadedCover;
  assert(books::makeShelfCover(picture,cover));
  assert(books::saveShelfCover(a,cover));
  assert(books::loadShelfCover(a,loadedCover));
  assert(cover.ink==loadedCover.ink);
  // Overwriting an EPUB must invalidate cached title/author metadata.
  FILE* f=std::fopen(a.c_str(),"ab");
  assert(f);
  assert(std::fputs("changed",f)>=0);
  assert(std::fclose(f)==0);
  assert(!books::loadShelfDetails(a,details));
  assert(!books::loadShelfCover(a,loadedCover));
  // Ignore malformed history rather than trusting arbitrary SD bytes.
  f=std::fopen(books::recentBooksFile().c_str(),"wb");
  assert(f);
  assert(std::fputs("BOOKS-RECENT1\nnot-hex\n",f)>=0);
  assert(std::fclose(f)==0);
  assert(books::loadRecentBooks().empty());
  assert(std::remove(a.c_str())==0);
  assert(std::remove(b.c_str())==0);
  return 0;
}
