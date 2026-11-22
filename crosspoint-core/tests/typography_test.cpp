// SPDX-License-Identifier: MIT
#include "Typography.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
int main() {
  char dir[]="/tmp/books-type-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  books::Typography t{};
  assert(!books::loadTypography(t));
  assert(books::saveTypography({books::Typography::Family::Serif,12}));
  assert(books::loadTypography(t));
  assert(t.family==books::Typography::Family::Serif && t.size==12);
  assert(books::typographyFontPath(t)==std::string(dir)+"/fonts/serif-12.cpfont");
  assert(!books::saveTypography({books::Typography::Family::Sans,17}));
  for(int point=10;point<=12;++point) {
    assert(books::validTypography({books::Typography::Family::Sans,point}));
    assert(books::validTypography({books::Typography::Family::Serif,point}));
    assert(books::saveTypography({books::Typography::Family::Serif,point}));
    assert(books::loadTypography(t));
    assert(t.family==books::Typography::Family::Serif && t.size==point);
    assert(books::typographyFontPath(t)==
           std::string(dir)+"/fonts/serif-"+std::to_string(point)+".cpfont");
    assert(books::typographyCjkFontPath(point)==
           std::string(dir)+"/fonts/cjk-"+std::to_string(point)+".cpfont");
    assert(books::nextTypographySize(point)==(point==12?10:point+1));
  }
  // CrossInk-inspired named families must remain selectable and persistent
  // for every supported point size. Old Sans/Serif slugs stay backward-compatible.
  const books::Typography::Family families[] = {
      books::Typography::Family::Sans,
      books::Typography::Family::Serif,
      books::Typography::Family::Bitter,
      books::Typography::Family::LexendDeca,
      books::Typography::Family::ChareInk,
      books::Typography::Family::Literata,
      books::Typography::Family::Merriweather,
      books::Typography::Family::Inter,
      books::Typography::Family::AtkinsonHyperlegible
  };
  for(auto family:families) {
    assert(books::typographyFamilySlug(family));
    assert(books::typographyFamilyName(family));
    for(int point=10;point<=12;++point) {
      const books::Typography selected{family,point};
      assert(books::validTypography(selected));
      assert(books::saveTypography(selected));
      assert(books::loadTypography(t));
      assert(t.family==family && t.size==point);
      assert(books::typographyFontPath(t)==std::string(dir)+
          "/fonts/"+books::typographyFamilySlug(family)+"-"+
          std::to_string(point)+".cpfont");
    }
  }
  assert(books::nextTypographyFamily(
      books::Typography::Family::AtkinsonHyperlegible)==
      books::Typography::Family::Sans);
  assert(!books::validTypography({
      books::Typography::Family::Count,12}));
  // Installed family picker skips unavailable packs without selecting ghosts.
  const std::string fontDir=std::string(dir)+"/fonts";
  assert(::mkdir(fontDir.c_str(),0700)==0);
  const std::string bitter=fontDir+"/bitter-12.cpfont";
  auto* installed=std::fopen(bitter.c_str(),"wb");assert(installed);
  assert(std::fputs("test\n",installed)>0);
  assert(std::fclose(installed)==0);
  assert(books::nextInstalledTypographyFamily({
      books::Typography::Family::Serif,12})==
      books::Typography::Family::Bitter);
  assert(std::remove(bitter.c_str())==0);
  assert(::rmdir(fontDir.c_str())==0);
  assert(!books::validTypography({books::Typography::Family::Sans,9}));
  assert(!books::validTypography({books::Typography::Family::Sans,13}));
  assert(!books::saveTypography({books::Typography::Family::Sans,9}));
  assert(!books::saveTypography({books::Typography::Family::Sans,13}));
  assert(books::typographyCjkFontPath(9).empty());
  assert(books::typographyCjkFontPath(13).empty());
  const auto path=std::string(dir)+"/typography.conf";
  for(int oldSize : {8,9}) {
    auto* old=std::fopen(path.c_str(),"wb");assert(old);
    assert(std::fprintf(old,"BOOKS-TYPE1 serif %d\n",oldSize)>0);
    assert(std::fclose(old)==0);
    assert(books::loadTypography(t));
    assert(t.family==books::Typography::Family::Serif && t.size==10);
  }
  auto* f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fputs("BOOKS-TYPE1 unknown 999\n",f)>0);
  assert(std::fclose(f)==0);
  assert(!books::loadTypography(t));
  f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fputs("BOOKS-TYPE1 serif 18\n",f)>0);
  assert(std::fclose(f)==0);
  assert(books::loadTypography(t));
  assert(t.family==books::Typography::Family::Serif && t.size==12);
  assert(std::remove(path.c_str())==0);
  assert(rmdir(dir)==0);
}
