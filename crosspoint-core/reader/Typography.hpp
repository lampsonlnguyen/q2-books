// SPDX-License-Identifier: MIT
#pragma once
// Q2-specific multi-family typography preferences. Font files are CrossPoint
// CPFONT v4: each selectable 8..14pt size MUST have its own raster/metrics.
// CrossInk's scalable-font menu inspired this range, not scaled 12pt bitmaps.
#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
namespace books {
struct Typography {
  // Append only: family IDs are embedded in CrossPoint Section render specs.
  enum class Family { Sans, Serif, Bitter, LexendDeca, ChareInk,
                      Literata, Merriweather, Inter, AtkinsonHyperlegible,
                      // RD-05: the reader's own fonts from the card's Fonts/
                      // folder, in name order (see userFontFamilies()).
                      User1, User2, User3, User4,
                      Count };
  Family family=Family::Sans;
  int size=12;
};
inline bool validTypography(const Typography& t) {
  return static_cast<unsigned>(t.family)<
         static_cast<unsigned>(Typography::Family::Count) &&
         t.size>=8 && t.size<=14;
}
// The Q2 Center button cycles font size through seven sizes.
inline int nextTypographySize(int current) {
  return current>=8 && current<14?current+1:8;
}
inline std::string typographyRoot() {
  const char* env=std::getenv("BOOKS_STATE_DIR");
  return env && *env ? env : "/mnt/mmc/.books";
}
// RD-05 user fonts: <Fonts>/<Name>-<size>.cpfont (CrossPoint CPFONT v4, made
// with CrossPoint's font tools), sizes 8 through 14; a family is offered when
// any supported file is there. Names: letters, digits, '_', '-', '.'; 24 at most.
constexpr int kUserFontSlots=4;
constexpr long kUserFontMaxBytes=16L<<20;
inline std::string userFontsDir() {
  const char* env=std::getenv("BOOKS_STATE_DIR");
  return env && *env ? std::string(env)+"/Fonts" : std::string("/mnt/mmc/Fonts");
}
inline bool validUserFontName(const std::string& name) {
  if(name.empty() || name.size()>24)return false;
  for(char c:name)
    if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return false;
  return true;
}
// A real CPFONT file of sane size.
inline bool usableCpfont(const std::string& path) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<64 ||
     st.st_size>kUserFontMaxBytes)return false;
  std::FILE* f=std::fopen(path.c_str(),"rb");
  if(!f)return false;
  char magic[8]={};
  const bool ok=std::fread(magic,1,8,f)==8 && std::memcmp(magic,"CPFONT\0\0",8)==0;
  std::fclose(f);
  return ok;
}
inline std::vector<std::string> userFontFamilies() {
  std::vector<std::string> names;
  DIR* d=opendir(userFontsDir().c_str());
  if(!d)return names;
  for(dirent* e;(e=readdir(d))!=nullptr;) {
    const std::string file=e->d_name;
    const auto dash=file.rfind('-');
    if(dash==std::string::npos)continue;
    const std::string suffix=file.substr(dash);
    bool supported=false;
    for(int size=8;size<=14;++size)
      if(suffix=="-"+std::to_string(size)+".cpfont")supported=true;
    if(!supported)continue;
    if(file.size()<=suffix.size() || file.compare(file.size()-suffix.size(),suffix.size(),suffix)!=0)continue;
    const std::string name=file.substr(0,file.size()-suffix.size());
    if(validUserFontName(name) && usableCpfont(userFontsDir()+"/"+file))names.push_back(name);
  }
  closedir(d);
  std::sort(names.begin(),names.end());
  names.erase(std::unique(names.begin(),names.end()),names.end());
  if(names.size()>static_cast<size_t>(kUserFontSlots))names.resize(kUserFontSlots);
  return names;
}
inline int userFontSlot(Typography::Family f) {
  const int v=static_cast<int>(f)-static_cast<int>(Typography::Family::User1);
  return v>=0 && v<kUserFontSlots?v:-1;
}
inline std::string userFontName(Typography::Family f) {
  const int slot=userFontSlot(f);
  if(slot<0)return {};
  const auto names=userFontFamilies();
  return slot<static_cast<int>(names.size())?names[slot]:std::string();
}
// The first two slugs preserve old installed assets and saved settings.
inline const char* typographyFamilySlug(Typography::Family f) {
  constexpr const char* names[]={
      "sans","serif","bitter","lexenddeca","chareink",
      "literata","merriweather","inter","atkinson"};
  const auto index=static_cast<unsigned>(f);
  return index<sizeof(names)/sizeof(names[0])?names[index]:nullptr;
}
// The saved name of a family: a built-in slug, or "user:<Name>".
inline std::string typographySavedName(Typography::Family f) {
  if(const char* slug=typographyFamilySlug(f))return slug;
  const auto name=userFontName(f);
  return name.empty()?std::string():"user:"+name;
}
inline std::string typographyFamilyName(Typography::Family f) {
  constexpr const char* names[]={
      "Noto Sans","Noto Serif","Bitter","Lexend Deca","ChareInk",
      "Literata","Merriweather","Inter","Atkinson"};
  const auto index=static_cast<unsigned>(f);
  if(index<sizeof(names)/sizeof(names[0]))return names[index];
  const auto user=userFontName(f);
  return user.empty()?std::string("Unknown"):user;
}
inline Typography::Family nextTypographyFamily(Typography::Family current) {
  const auto value=static_cast<unsigned>(current);
  return static_cast<Typography::Family>(
      (value+1)%static_cast<unsigned>(Typography::Family::Count));
}
inline std::string typographyFontPath(Typography t) {
  if(!validTypography(t))return {};
  if(userFontSlot(t.family)>=0) {
    const auto name=userFontName(t.family);
    if(name.empty())return {};
    const std::string path=userFontsDir()+"/"+name+"-"+std::to_string(t.size)+".cpfont";
    return usableCpfont(path)?path:std::string();
  }
  return typographyRoot()+"/fonts/"+typographyFamilySlug(t.family)+"-"+
         std::to_string(t.size)+".cpfont";
}
// Part of the layout cache's identity: a user font's file can change under
// the same slot (or another file take the slot), built-in fonts cannot.
inline std::string typographyFontIdentity(Typography t) {
  if(userFontSlot(t.family)<0)return {};
  struct stat st{};
  const auto path=typographyFontPath(t);
  if(path.empty() || ::stat(path.c_str(),&st)!=0)return {};
  return path+":"+std::to_string(static_cast<long long>(st.st_size))+":"+
         std::to_string(static_cast<long long>(st.st_mtime));
}
// Shared CJK fallback is the original CrossPoint NotoSansCJKJP .cpfont v4.
// This does not replace the reader's selected Sans/Serif face for Latin text.
inline std::string typographyCjkFontPath(int size) {
  if(size<8 || size>14)return {};
  return typographyRoot()+"/fonts/cjk-"+std::to_string(size)+".cpfont";
}
inline bool installedTypographySize(Typography t) {
  if(!validTypography(t))return false;
  if(t.family==Typography::Family::Sans && t.size==12)return true;
  return usableCpfont(typographyFontPath(t));
}
inline int nearestTypographySize(Typography t) {
  int best=0;
  for(int size=8;size<=14;++size)
    if(installedTypographySize({t.family,size}) &&
       (!best || std::abs(size-t.size)<std::abs(best-t.size)))best=size;
  return best;
}
inline int nextInstalledTypographySize(Typography t) {
  int size=t.size;
  for(int i=0;i<7;++i) {
    size=nextTypographySize(size);
    if(installedTypographySize({t.family,size}))return size;
  }
  return t.size;
}
// Skip uninstalled choices rather than changing settings to a missing font.
inline Typography::Family nextInstalledTypographyFamily(Typography t) {
  auto next=t.family;
  for(unsigned i=0;i<static_cast<unsigned>(Typography::Family::Count);++i) {
    next=nextTypographyFamily(next);
    if(next==t.family)return t.family;
    if(nearestTypographySize({next,t.size}))return next;
  }
  return t.family;
}
inline bool loadTypography(Typography& value) {
  const std::string path=typographyRoot()+"/typography.conf";
  std::FILE* f=std::fopen(path.c_str(),"rb");
  if(!f)return false;
  char family[40]={};
  int size=0;
  const int count=std::fscanf(f,"BOOKS-TYPE1 %39s %d",family,&size);
  std::fclose(f);
  if(count!=2)return false;
  Typography read{};
  read.size=size;
  bool found=false;
  for(unsigned i=0;i<static_cast<unsigned>(Typography::Family::Count);++i) {
    const auto item=static_cast<Typography::Family>(i);
    const auto saved=typographySavedName(item);
    if(!saved.empty() && saved==family) {
      read.family=item;
      found=true;
      break;
    }
  }
  if(!found)return false;
  // Preserve valid preferences and clamp older oversized settings.
  read.size=std::clamp(read.size,8,14);
  if(!validTypography(read))return false;
  value=read;
  return true;
}
inline bool saveTypography(Typography t) {
  if(!validTypography(t) || typographySavedName(t.family).empty())return false;
  const std::string filename=typographyRoot()+"/typography.conf";
  const std::string temp=filename+".tmp";
  std::FILE* f=std::fopen(temp.c_str(),"wb");
  if(!f)return false;
  const bool wrote=std::fprintf(f,"BOOKS-TYPE1 %s %d\n",
      typographySavedName(t.family).c_str(),t.size)>0;
  bool ok=wrote;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),filename.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace books
