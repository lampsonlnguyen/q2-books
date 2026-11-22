// SPDX-License-Identifier: MIT
#include "ReadingPosition.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
int main() {
  char dir[]="/tmp/books-progress-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string book=std::string(dir)+"/A Vietnamese book.epub";
  { auto* f=std::fopen(book.c_str(),"wb"); assert(f);
    assert(std::fwrite("epub",1,4,f)==4); assert(std::fclose(f)==0); }
  books::ReadingPosition position{};
  assert(!books::loadReadingPosition(book,position));
  assert(books::saveReadingPosition(book,{3,42}));
  assert(books::loadReadingPosition(book,position));
  assert(position.spine==3 && position.page==42);
  assert(!books::saveReadingPosition(book,{-1,0}));
  // Version 2 keeps the engine, the exact text offset and the chapter fraction.
  assert(books::saveReadingPosition(book,{5,17,'o',123456,750000}));
  assert(books::loadReadingPosition(book,position));
  assert(position.spine==5 && position.page==17 && position.engine=='o' &&
         position.textOffset==123456 && position.fractionPpm==750000);
  assert(books::saveReadingPosition(book,{2,9,'l',0,2000000}));  // clamped
  assert(books::loadReadingPosition(book,position));
  assert(position.engine=='l' && position.fractionPpm==1000000);
  // A version 1 file (no engine or anchor) still resumes by page.
  { struct stat st{}; assert(stat(book.c_str(),&st)==0);
    auto* f=std::fopen(books::positionFile(book).c_str(),"wb"); assert(f);
    std::fprintf(f,"BOOKS1 %lld %lld 4 11\n",(long long)st.st_size,(long long)st.st_mtime);
    assert(std::fclose(f)==0); }
  assert(books::loadReadingPosition(book,position));
  assert(position.spine==4 && position.page==11 && position.engine=='?' && position.fractionPpm==0);
  // Garbage in the new fields is refused, not half-read.
  { struct stat st{}; assert(stat(book.c_str(),&st)==0);
    auto* f=std::fopen(books::positionFile(book).c_str(),"wb"); assert(f);
    std::fprintf(f,"BOOKS2 %lld %lld 4 11 x 5 5\n",(long long)st.st_size,(long long)st.st_mtime);
    assert(std::fclose(f)==0); }
  assert(!books::loadReadingPosition(book,position));
  // A changed EPUB invalidates the old page index.
  { auto* f=std::fopen(book.c_str(),"ab"); assert(f);
    assert(std::fwrite("new",1,3,f)==3); assert(std::fclose(f)==0); }
  assert(!books::loadReadingPosition(book,position));
  std::remove(books::positionFile(book).c_str());
  std::remove(book.c_str());
  assert(rmdir(dir)==0);
}
