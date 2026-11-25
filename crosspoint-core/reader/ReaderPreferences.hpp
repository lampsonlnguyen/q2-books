// SPDX-License-Identifier: MIT
#pragma once
// Versioned, atomic SD-card preferences. No firmware/app framework dependency.
// Changes to these fields invalidate original CrossPoint Section cache via
// ReaderRenderSpec's existing comparison, not manual cache deletion.
#include "ReadingPosition.hpp"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace books {
// Indexes are persisted in reading-settings.conf.
struct PageWarmth {
  const char* label;
  uint32_t paper;
};
inline constexpr PageWarmth pageWarmthChoices[]={{"Cool",0xdeeaff},{"Default",0},{"Warm",0xffeed5},{"Warmer",0xffdcb0}};
inline constexpr int kDefaultPageWarmth=1;
inline constexpr int kPageWarmthCount=sizeof(pageWarmthChoices)/sizeof(pageWarmthChoices[0]);

struct ReaderPreferences {
  int margin=14; // pixels on both sides; supports 14, 22, 30.
  int linePercent=100; // 100, 120, 140 (CrossPoint lineCompression)
  int alignment=0; // CrossPoint paragraphAlignment 0=left/auto, 1=justify
  bool hyphenation=false;
  bool embeddedStyle=true;
  // Night mode: kNightAuto follows Q2 Pod's theme (a dark theme reads in
  // night mode), else the reader's own choice, kept only where it differs
  // from the theme (toggleNight).
  int night=-1;
  bool focusReading=false; // upstream CrossPoint Focus Reading (partial bold)
  int autoTurnSeconds=0;   // 0 = off; see autoTurnChoices
  bool bookFonts=true;
  int pageWarmth=kDefaultPageWarmth;
};
// Auto page turn intervals. Q2 Pod closes a card app after 2 minutes with no
// input unless Smart Awake is on, so longer reading without touching the Q2
// needs Smart Awake whatever the interval.
inline constexpr int autoTurnChoices[]={0,10,15,20,30,45,60};
inline constexpr int kNightAuto=-1,kNightOff=0,kNightOn=1;
// Whether pages show in night mode (dark paper) under a light or dark theme.
inline bool nightActive(const ReaderPreferences& p,bool systemDark) {
  return p.night==kNightAuto?systemDark:p.night==kNightOn;
}
// The Night mode row: flip what shows; matching the theme again follows it.
inline void toggleNight(ReaderPreferences& p,bool systemDark) {
  const bool next=!nightActive(p,systemDark);
  p.night=next==systemDark?kNightAuto:(next?kNightOn:kNightOff);
}
inline uint32_t readingPaperTint(const ReaderPreferences& p,bool systemDark) {
  return nightActive(p,systemDark)?0:pageWarmthChoices[p.pageWarmth].paper;
}
inline bool validReaderPreferences(const ReaderPreferences& p) {
  bool autoTurnOk=false;
  for(int choice:autoTurnChoices)autoTurnOk=autoTurnOk||p.autoTurnSeconds==choice;
  return (p.margin==14||p.margin==22||p.margin==30) &&
         (p.linePercent==100||p.linePercent==120||p.linePercent==140) &&
         (p.alignment==0||p.alignment==1) && autoTurnOk &&
         p.pageWarmth>=0 && p.pageWarmth<kPageWarmthCount &&
         (p.night==kNightAuto||p.night==kNightOff||p.night==kNightOn);
}
inline std::string readerPreferencesFile() {
  const char* root=std::getenv("BOOKS_STATE_DIR");
  return std::string(root&&*root?root:"/mnt/mmc/.books")+"/reading-settings.conf";
}
inline bool loadReaderPreferences(ReaderPreferences& value) {
  FILE* f=std::fopen(readerPreferencesFile().c_str(),"rb");
  if(!f)return false;
  char magic[32]{};
  int margin=0,spacing=0,align=0,hyphen=0,css=0,night=0,focus=0,autoTurn=0,bookFonts=1;
  int pageWarmth=kDefaultPageWarmth;
  const int n=std::fscanf(f,"%31s %d %d %d %d %d %d %d %d %d %d",
       magic,&margin,&spacing,&align,&hyphen,&css,&night,&focus,&autoTurn,&bookFonts,&pageWarmth);
  std::fclose(f);
  const bool v1=std::string(magic)=="BOOKS-SETTINGS1" && n==6;
  const bool v2=std::string(magic)=="BOOKS-SETTINGS2" && n==7;
  const bool v3=std::string(magic)=="BOOKS-SETTINGS3" && n==9;
  // v4: night 0 follows the theme, 1 off, 2 on. Before, 0 was the default
  // off (taken as follow the theme) and 1 on.
  const bool v4=std::string(magic)=="BOOKS-SETTINGS4" && n==9;
  const bool v5=std::string(magic)=="BOOKS-SETTINGS5" && n==10;
  const bool v6=std::string(magic)=="BOOKS-SETTINGS6" && n==11;
  const bool explicitNight=v4||v5||v6;
  if(!v3&&!explicitNight){focus=0;autoTurn=0;}
  if(night<0||night>(explicitNight?2:1))return false;
  const int choice=explicitNight?(night==0?kNightAuto:night==1?kNightOff:kNightOn):(night?kNightOn:kNightAuto);
  ReaderPreferences temp{margin,spacing,align,hyphen!=0,css!=0,choice,focus!=0,autoTurn,bookFonts!=0,pageWarmth};
  if((!v1&&!v2&&!v3&&!explicitNight)||hyphen<0||hyphen>1||css<0||css>1||
     focus<0||focus>1||bookFonts<0||bookFonts>1||!validReaderPreferences(temp))return false;
  value=temp;
  return true;
}
inline bool saveReaderPreferences(const ReaderPreferences& value) {
  if(!validReaderPreferences(value))return false;
  const std::string name=readerPreferencesFile(),tmp=name+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  const int night=value.night==kNightAuto?0:value.night==kNightOff?1:2;
  bool ok=std::fprintf(f,"BOOKS-SETTINGS6 %d %d %d %d %d %d %d %d %d %d\n",
    value.margin,value.linePercent,value.alignment,
    value.hyphenation?1:0,value.embeddedStyle?1:0,night,
    value.focusReading?1:0,value.autoTurnSeconds,value.bookFonts?1:0,value.pageWarmth)>0;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok&&std::rename(tmp.c_str(),name.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
}
} // namespace books
