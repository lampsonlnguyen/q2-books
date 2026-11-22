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
    assert(!books::typographyFamilyName(family).empty());
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
      books::Typography::Family::User1);
  assert(books::nextTypographyFamily(books::Typography::Family::User4)==
      books::Typography::Family::Sans);
  // RD-05: user fonts from <state>/Fonts (the card's Fonts/ folder).
  {
    const std::string userDir=std::string(dir)+"/Fonts";
    assert(::mkdir(userDir.c_str(),0700)==0);
    const auto put=[&](const std::string& name,bool real) {
      auto* f=std::fopen((userDir+"/"+name).c_str(),"wb");assert(f);
      if(real)assert(std::fwrite("CPFONT\0\0",1,8,f)==8);
      for(int i=0;i<100;++i)std::fputc('x',f);
      assert(std::fclose(f)==0);
    };
    put("My_Font-12.cpfont",true);
    put("My_Font-10.cpfont",true);
    put("Zed-12.cpfont",true);
    put("Bad Name-12.cpfont",true);   // a space: not offered
    put("Junk-12.cpfont",false);      // not a CPFONT
    put("Other-11.cpfont",true);      // no 12 pt: not offered
    const auto names=books::userFontFamilies();
    assert(names.size()==2 && names[0]=="My_Font" && names[1]=="Zed");
    const books::Typography mine{books::Typography::Family::User1,10};
    assert(books::typographyFamilyName(mine.family)=="My_Font");
    assert(books::typographyFontPath(mine)==userDir+"/My_Font-10.cpfont");
    assert(books::typographyFontPath({books::Typography::Family::User1,11}).empty());  // no 11 pt file
    assert(books::typographyFontPath({books::Typography::Family::User3,12}).empty());  // no third font
    assert(!books::typographyFontIdentity(mine).empty());
    assert(books::typographyFontIdentity({books::Typography::Family::Sans,12}).empty());
    assert(books::saveTypography(mine));
    books::Typography loaded{};
    assert(books::loadTypography(loaded) && loaded.family==mine.family && loaded.size==10);
    assert(books::nextInstalledTypographyFamily({books::Typography::Family::AtkinsonHyperlegible,12})==
           books::Typography::Family::User1);
    assert(books::nextInstalledTypographyFamily({books::Typography::Family::User1,12})==
           books::Typography::Family::User2);
    assert(!books::saveTypography({books::Typography::Family::User3,12}));  // empty slot
    // The font removed: the saved choice no longer loads (the reader falls back).
    assert(std::remove((userDir+"/My_Font-12.cpfont").c_str())==0);
    assert(!books::loadTypography(loaded));
    std::system(("rm -rf '"+userDir+"' '"+std::string(dir)+"/typography.conf'").c_str());
  }
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
