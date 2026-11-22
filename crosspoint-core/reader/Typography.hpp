// SPDX-License-Identifier: MIT
#pragma once
// Q2-specific multi-family typography preferences. Font files are CrossPoint
// CPFONT v4: each selectable 10..12pt size MUST have its own raster/metrics.
// CrossInk's scalable-font menu inspired this range, not scaled 12pt bitmaps.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace crosspod {
struct Typography {
  // Append only: family IDs are embedded in CrossPoint Section render specs.
  enum class Family { Sans, Serif, Bitter, LexendDeca, ChareInk,
                      Literata, Merriweather, Inter, AtkinsonHyperlegible,
                      Count };
  Family family=Family::Sans;
  int size=12;
};
inline bool validTypography(const Typography& t) {
  return static_cast<unsigned>(t.family)<
         static_cast<unsigned>(Typography::Family::Count) &&
         t.size>=10 && t.size<=12;
}
// The Q2 Center button cycles font size through exactly three sizes.
inline int nextTypographySize(int current) {
  return current>=10 && current<12?current+1:10;
}
// The first two slugs preserve old installed assets and saved settings.
inline const char* typographyFamilySlug(Typography::Family f) {
  constexpr const char* names[]={
      "sans","serif","bitter","lexenddeca","chareink",
      "literata","merriweather","inter","atkinson"};
  const auto index=static_cast<unsigned>(f);
  return index<sizeof(names)/sizeof(names[0])?names[index]:nullptr;
}
inline const char* typographyFamilyName(Typography::Family f) {
  constexpr const char* names[]={
      "Noto Sans","Noto Serif","Bitter","Lexend Deca","ChareInk",
      "Literata","Merriweather","Inter","Atkinson"};
  const auto index=static_cast<unsigned>(f);
  return index<sizeof(names)/sizeof(names[0])?names[index]:"Unknown";
}
inline Typography::Family nextTypographyFamily(Typography::Family current) {
  const auto value=static_cast<unsigned>(current);
  return static_cast<Typography::Family>(
      (value+1)%static_cast<unsigned>(Typography::Family::Count));
}
inline std::string typographyRoot() {
  const char* env=std::getenv("CROSSPOD_STATE_DIR");
  return env && *env ? env : "/mnt/mmc/.crosspod";
}
inline std::string typographyFontPath(Typography t) {
  if(!validTypography(t))return {};
  return typographyRoot()+"/fonts/"+typographyFamilySlug(t.family)+"-"+
         std::to_string(t.size)+".cpfont";
}
// Shared CJK fallback is the original CrossPoint NotoSansCJKJP .cpfont v4.
// This does not replace the reader's selected Sans/Serif face for Latin text.
inline std::string typographyCjkFontPath(int size) {
  if(size<10 || size>12)return {};
  return typographyRoot()+"/fonts/cjk-"+std::to_string(size)+".cpfont";
}
// Skip uninstalled choices rather than changing settings to a missing font.
inline Typography::Family nextInstalledTypographyFamily(Typography t) {
  auto next=t.family;
  for(unsigned i=0;i<static_cast<unsigned>(Typography::Family::Count);++i) {
    next=nextTypographyFamily(next);
    if(next==t.family)return t.family;
    const Typography candidate{next,t.size};
    if(::access(typographyFontPath(candidate).c_str(),R_OK)==0)return next;
  }
  return t.family;
}
inline bool loadTypography(Typography& value) {
  const std::string path=typographyRoot()+"/typography.conf";
  std::FILE* f=std::fopen(path.c_str(),"rb");
  if(!f)return false;
  char family[32]={};
  int size=0;
  const int count=std::fscanf(f,"CROSSPOD-TYPE1 %31s %d",family,&size);
  std::fclose(f);
  if(count!=2)return false;
  Typography read{};
  read.size=size;
  bool found=false;
  for(unsigned i=0;i<static_cast<unsigned>(Typography::Family::Count);++i) {
    const auto item=static_cast<Typography::Family>(i);
    if(std::strcmp(family,typographyFamilySlug(item))==0) {
      read.family=item;
      found=true;
      break;
    }
  }
  if(!found)return false;
  // Any old saved 14/16/18pt preference is reduced to the 12pt ceiling;
  // existing 10..12pt choices are preserved; prior 8/9pt beta choices migrate to 10pt.
  // Keep the existing file format so upgrades do not erase other settings.
  if(read.size>12)read.size=12;
  if(read.size==8 || read.size==9)read.size=10;
  if(!validTypography(read))return false;
  value=read;
  return true;
}
inline bool saveTypography(Typography t) {
  if(!validTypography(t))return false;
  const std::string filename=typographyRoot()+"/typography.conf";
  const std::string temp=filename+".tmp";
  std::FILE* f=std::fopen(temp.c_str(),"wb");
  if(!f)return false;
  const bool wrote=std::fprintf(f,"CROSSPOD-TYPE1 %s %d\n",
      typographyFamilySlug(t.family),t.size)>0;
  bool ok=wrote;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),filename.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace crosspod
