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
  for(int point=8;point<=12;++point) {
    assert(crosspod::validTypography({crosspod::Typography::Family::Sans,point}));
    assert(crosspod::validTypography({crosspod::Typography::Family::Serif,point}));
    assert(crosspod::saveTypography({crosspod::Typography::Family::Serif,point}));
    assert(crosspod::loadTypography(t));
    assert(t.family==crosspod::Typography::Family::Serif && t.size==point);
    assert(crosspod::typographyFontPath(t)==
           std::string(dir)+"/fonts/serif-"+std::to_string(point)+".cpfont");
    assert(crosspod::typographyCjkFontPath(point)==
           std::string(dir)+"/fonts/cjk-"+std::to_string(point)+".cpfont");
    assert(crosspod::nextTypographySize(point)==(point==12?8:point+1));
  }
  assert(!crosspod::validTypography({crosspod::Typography::Family::Sans,7}));
  assert(!crosspod::validTypography({crosspod::Typography::Family::Sans,13}));
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,7}));
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,13}));
  assert(crosspod::typographyCjkFontPath(7).empty());
  assert(crosspod::typographyCjkFontPath(13).empty());
  const auto path=std::string(dir)+"/typography.conf";
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
