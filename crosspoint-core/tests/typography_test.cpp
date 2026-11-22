// SPDX-License-Identifier: MIT
#include "Typography.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>
int main() {
  char dir[]="/tmp/crosspod-type-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("CROSSPOD_STATE_DIR",dir,1)==0);
  crosspod::Typography t{};
  assert(!crosspod::loadTypography(t));
  assert(crosspod::saveTypography({crosspod::Typography::Family::Serif,12}));
  assert(crosspod::loadTypography(t));
  assert(t.family==crosspod::Typography::Family::Serif && t.size==12);
  assert(crosspod::typographyFontPath(t)==std::string(dir)+"/fonts/serif-12.cpfont");
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,17}));
  for(int point=10;point<=12;++point) {
    assert(crosspod::validTypography({crosspod::Typography::Family::Sans,point}));
    assert(crosspod::validTypography({crosspod::Typography::Family::Serif,point}));
    assert(crosspod::saveTypography({crosspod::Typography::Family::Serif,point}));
    assert(crosspod::loadTypography(t));
    assert(t.family==crosspod::Typography::Family::Serif && t.size==point);
    assert(crosspod::typographyFontPath(t)==
           std::string(dir)+"/fonts/serif-"+std::to_string(point)+".cpfont");
    assert(crosspod::typographyCjkFontPath(point)==
           std::string(dir)+"/fonts/cjk-"+std::to_string(point)+".cpfont");
    assert(crosspod::nextTypographySize(point)==(point==12?10:point+1));
  }
  // CrossInk-inspired named families must remain selectable and persistent
  // for every supported point size. Old Sans/Serif slugs stay backward-compatible.
  const crosspod::Typography::Family families[] = {
      crosspod::Typography::Family::Sans,
      crosspod::Typography::Family::Serif,
      crosspod::Typography::Family::Bitter,
      crosspod::Typography::Family::LexendDeca,
      crosspod::Typography::Family::ChareInk,
      crosspod::Typography::Family::Literata,
      crosspod::Typography::Family::Merriweather,
      crosspod::Typography::Family::Inter,
      crosspod::Typography::Family::AtkinsonHyperlegible
  };
  for(auto family:families) {
    assert(crosspod::typographyFamilySlug(family));
    assert(crosspod::typographyFamilyName(family));
    for(int point=10;point<=12;++point) {
      const crosspod::Typography selected{family,point};
      assert(crosspod::validTypography(selected));
      assert(crosspod::saveTypography(selected));
      assert(crosspod::loadTypography(t));
      assert(t.family==family && t.size==point);
      assert(crosspod::typographyFontPath(t)==std::string(dir)+
          "/fonts/"+crosspod::typographyFamilySlug(family)+"-"+
          std::to_string(point)+".cpfont");
    }
  }
  assert(crosspod::nextTypographyFamily(
      crosspod::Typography::Family::AtkinsonHyperlegible)==
      crosspod::Typography::Family::Sans);
  assert(!crosspod::validTypography({
      crosspod::Typography::Family::Count,12}));
  // Installed family picker skips unavailable packs without selecting ghosts.
  const std::string fontDir=std::string(dir)+"/fonts";
  assert(::mkdir(fontDir.c_str(),0700)==0);
  const std::string bitter=fontDir+"/bitter-12.cpfont";
  auto* installed=std::fopen(bitter.c_str(),"wb");assert(installed);
  assert(std::fputs("test\n",installed)>0);
  assert(std::fclose(installed)==0);
  assert(crosspod::nextInstalledTypographyFamily({
      crosspod::Typography::Family::Serif,12})==
      crosspod::Typography::Family::Bitter);
  assert(std::remove(bitter.c_str())==0);
  assert(::rmdir(fontDir.c_str())==0);
  assert(!crosspod::validTypography({crosspod::Typography::Family::Sans,9}));
  assert(!crosspod::validTypography({crosspod::Typography::Family::Sans,13}));
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,9}));
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,13}));
  assert(crosspod::typographyCjkFontPath(9).empty());
  assert(crosspod::typographyCjkFontPath(13).empty());
  const auto path=std::string(dir)+"/typography.conf";
  for(int oldSize : {8,9}) {
    auto* old=std::fopen(path.c_str(),"wb");assert(old);
    assert(std::fprintf(old,"CROSSPOD-TYPE1 serif %d\n",oldSize)>0);
    assert(std::fclose(old)==0);
    assert(crosspod::loadTypography(t));
    assert(t.family==crosspod::Typography::Family::Serif && t.size==10);
  }
  auto* f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fputs("CROSSPOD-TYPE1 unknown 999\n",f)>0);
  assert(std::fclose(f)==0);
  assert(!crosspod::loadTypography(t));
  f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fputs("CROSSPOD-TYPE1 serif 18\n",f)>0);
  assert(std::fclose(f)==0);
  assert(crosspod::loadTypography(t));
  assert(t.family==crosspod::Typography::Family::Serif && t.size==12);
  assert(std::remove(path.c_str())==0);
  assert(rmdir(dir)==0);
}
