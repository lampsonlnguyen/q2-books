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
  assert(crosspod::saveTypography({crosspod::Typography::Family::Serif,16}));
  assert(crosspod::loadTypography(t));
  assert(t.family==crosspod::Typography::Family::Serif && t.size==16);
  assert(crosspod::typographyFontPath(t)==std::string(dir)+"/fonts/serif-16.cpfont");
  assert(!crosspod::saveTypography({crosspod::Typography::Family::Sans,17}));
  const auto path=std::string(dir)+"/typography.conf";
  auto* f=std::fopen(path.c_str(),"wb");assert(f);
  assert(std::fputs("CROSSPOD-TYPE1 unknown 999\n",f)>0);
  assert(std::fclose(f)==0);
  assert(!crosspod::loadTypography(t));
  assert(std::remove(path.c_str())==0);
  assert(rmdir(dir)==0);
}
