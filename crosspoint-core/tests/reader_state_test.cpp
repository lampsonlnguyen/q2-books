// SPDX-License-Identifier: MIT
#include "ReaderPreferences.hpp"
#include "Typography.hpp"
#include "Bookmarks.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <unistd.h>
int main() {
  char root[]="/tmp/books-state-tests-XXXXXX";
  char* dir=mkdtemp(root);
  assert(dir);
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string book=std::string(dir)+"/book.epub";
  FILE* f=std::fopen(book.c_str(),"wb");
  assert(f);
  assert(std::fwrite("book",1,4,f)==4);
  assert(std::fclose(f)==0);

  // A normal app exit must persist the exact page, not just the book title.
  books::ReadingPosition restored{};
  assert(!books::loadReadingPosition(book,restored));
  assert(books::saveReadingPosition(book,{2,17}));
  assert(books::loadReadingPosition(book,restored));
  assert(restored.spine==2 && restored.page==17);
  assert(books::saveReadingPosition(book,{2,18}));
  assert(books::loadReadingPosition(book,restored));
  assert(restored.spine==2 && restored.page==18);
  assert(!books::saveReadingPosition(book,{-1,0}));
  assert(!books::saveReadingPosition(book,{0,1000001}));

  // UI font is independent; persisted oversized reader settings clamp to 14pt.
  books::Typography textSize{};
  assert(books::validTypography(textSize));
  books::Typography oversizedSans{books::Typography::Family::Sans,15};
  books::Typography oversizedSerif{books::Typography::Family::Serif,18};
  assert(!books::validTypography(oversizedSans));
  assert(!books::validTypography(oversizedSerif));
  {
    const std::string oldSetting=std::string(dir)+"/typography.conf";
    FILE* old=std::fopen(oldSetting.c_str(),"wb");
    assert(old);
    assert(std::fputs("BOOKS-TYPE1 sans 18\\n",old)>0);
    assert(std::fclose(old)==0);
    assert(books::loadTypography(textSize));
    assert(textSize.size==14);
    std::remove(oldSetting.c_str());
  }
  books::ReaderPreferences defaults;
  assert(!books::loadReaderPreferences(defaults));
  assert(defaults.margin==14 && defaults.linePercent==100);
  books::ReaderPreferences chosen{22,140,1,true,false};
  assert(books::validReaderPreferences(chosen));
  assert(books::saveReaderPreferences(chosen));
  assert(books::loadReaderPreferences(defaults));
  assert(defaults.margin==22 && defaults.linePercent==140 &&
         defaults.alignment==1 && defaults.hyphenation && !defaults.embeddedStyle);
  auto invalid=chosen;invalid.margin=100;
  assert(!books::saveReaderPreferences(invalid));
  // SETTINGS3: focus reading and auto turn persist; v2 files still load with
  // both off; an interval outside the menu's choices is refused.
  chosen.focusReading=true;chosen.autoTurnSeconds=30;
  assert(books::saveReaderPreferences(chosen));
  defaults=books::ReaderPreferences{};
  assert(books::loadReaderPreferences(defaults));
  assert(defaults.focusReading && defaults.autoTurnSeconds==30 && defaults.margin==22);
  auto badTurn=chosen;badTurn.autoTurnSeconds=7;
  assert(!books::validReaderPreferences(badTurn));
  {
    FILE* v2=std::fopen(books::readerPreferencesFile().c_str(),"wb");
    assert(v2);
    assert(std::fputs("BOOKS-SETTINGS2 30 120 0 1 1 1\n",v2)>0);
    assert(std::fclose(v2)==0);
    defaults=books::ReaderPreferences{};
    defaults.focusReading=true;defaults.autoTurnSeconds=10;
    assert(books::loadReaderPreferences(defaults));
    assert(defaults.margin==30 && defaults.night==books::kNightOn && !defaults.focusReading &&
           defaults.autoTurnSeconds==0);
  }
  // Night mode follows Q2 Pod's theme until the reader picks the other look,
  // and following it again goes back to following.
  {
    books::ReaderPreferences p;
    assert(p.night==books::kNightAuto);
    assert(!books::nightActive(p,false) && books::nightActive(p,true));
    books::toggleNight(p,true);   // dark theme, reader wants light pages
    assert(p.night==books::kNightOff && !books::nightActive(p,true));
    assert(books::saveReaderPreferences(p));
    books::ReaderPreferences back;
    assert(books::loadReaderPreferences(back) && back.night==books::kNightOff);
    books::toggleNight(p,true);   // back to the theme's look
    assert(p.night==books::kNightAuto && books::nightActive(p,true));
    books::toggleNight(p,false);  // light theme, reader wants night
    assert(p.night==books::kNightOn && books::nightActive(p,false));
    assert(books::saveReaderPreferences(p));
    assert(books::loadReaderPreferences(back) && back.night==books::kNightOn);
    p.night=books::kNightAuto;
    assert(books::saveReaderPreferences(p));
    assert(books::loadReaderPreferences(back) && back.night==books::kNightAuto);
    // A v3 file: 0 was the default (follow the theme now), 1 night on.
    FILE* v3=std::fopen(books::readerPreferencesFile().c_str(),"wb");
    assert(v3 && std::fputs("BOOKS-SETTINGS3 14 100 0 0 1 0 0 0\n",v3)>0 && std::fclose(v3)==0);
    assert(books::loadReaderPreferences(back) && back.night==books::kNightAuto);
    FILE* bad=std::fopen(books::readerPreferencesFile().c_str(),"wb");
    assert(bad && std::fputs("BOOKS-SETTINGS4 14 100 0 0 1 3 0 0\n",bad)>0 && std::fclose(bad)==0);
    assert(!books::loadReaderPreferences(back));
  }

  assert(books::loadBookmarks(book).empty());
  assert(books::addBookmark(book,{0,3,100}));
  assert(books::addBookmark(book,{0,3,100})); // de-dup
  assert(books::addBookmark(book,{2,5,890}));
  auto marks=books::loadBookmarks(book);
  assert(marks.size()==2);
  assert(marks[0].spine==0&&marks[0].page==3&&marks[0].textOffset==100);
  assert(marks[1].spine==2&&marks[1].page==5&&marks[1].textOffset==890);
  for(int i=3;i<=32;++i)
    assert(books::addBookmark(book,{i,static_cast<size_t>(i),static_cast<uint32_t>(i*200)}));
  assert(books::loadBookmarks(book).size()==32);
  assert(!books::addBookmark(book,{33,33,3333})); // fixed limit

  // Modification changes identity and invalidates stale bookmarks.
  f=std::fopen(book.c_str(),"ab");
  assert(f);assert(std::fwrite("x",1,1,f)==1);std::fclose(f);
  assert(books::loadBookmarks(book).empty());
  assert(!books::loadReadingPosition(book,restored));
  std::remove(books::positionFile(book).c_str());
  std::remove(books::bookmarksFile(book).c_str());
  std::remove(books::readerPreferencesFile().c_str());
  std::remove(book.c_str());
  assert(rmdir(dir)==0);
  return 0;
}
