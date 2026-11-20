// SPDX-License-Identifier: MIT
#include "ReadingPosition.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
int main() {
  char dir[]="/tmp/crosspod-progress-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("CROSSPOD_STATE_DIR",dir,1)==0);
  const std::string book=std::string(dir)+"/A Vietnamese book.epub";
  { auto* f=std::fopen(book.c_str(),"wb"); assert(f);
    assert(std::fwrite("epub",1,4,f)==4); assert(std::fclose(f)==0); }
  crosspod::ReadingPosition position{};
  assert(!crosspod::loadReadingPosition(book,position));
  assert(crosspod::saveReadingPosition(book,{3,42}));
  assert(crosspod::loadReadingPosition(book,position));
  assert(position.spine==3 && position.page==42);
  assert(!crosspod::saveReadingPosition(book,{-1,0}));
  // A changed EPUB invalidates the old page index.
  { auto* f=std::fopen(book.c_str(),"ab"); assert(f);
    assert(std::fwrite("new",1,3,f)==3); assert(std::fclose(f)==0); }
  assert(!crosspod::loadReadingPosition(book,position));
  std::remove(crosspod::positionFile(book).c_str());
  std::remove(book.c_str());
  assert(rmdir(dir)==0);
}
