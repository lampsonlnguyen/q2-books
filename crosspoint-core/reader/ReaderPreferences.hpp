// SPDX-License-Identifier: MIT
#pragma once
// Versioned, atomic SD-card preferences. No firmware/app framework dependency.
// Changes to these fields invalidate original CrossPoint Section cache via
// ReaderRenderSpec's existing comparison, not manual cache deletion.
#include "ReadingPosition.hpp"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <unistd.h>

namespace books {
struct ReaderPreferences {
  int margin=14; // pixels on both sides; supports 14, 22, 30.
  int linePercent=100; // 100, 120, 140 (CrossPoint lineCompression)
  int alignment=0; // CrossPoint paragraphAlignment 0=left/auto, 1=justify
  bool hyphenation=false;
  bool embeddedStyle=true;
  bool nightMode=false;
  bool focusReading=false; // upstream CrossPoint Focus Reading (partial bold)
  int autoTurnSeconds=0;   // 0 = off; see autoTurnChoices
};
// Auto page turn intervals. Q2 Pod closes a card app after 2 minutes with no
// input unless Smart Awake is on, so longer reading without touching the Q2
// needs Smart Awake whatever the interval.
inline constexpr int autoTurnChoices[]={0,10,15,20,30,45,60};
inline bool validReaderPreferences(const ReaderPreferences& p) {
  bool autoTurnOk=false;
  for(int choice:autoTurnChoices)autoTurnOk=autoTurnOk||p.autoTurnSeconds==choice;
  return (p.margin==14||p.margin==22||p.margin==30) &&
         (p.linePercent==100||p.linePercent==120||p.linePercent==140) &&
         (p.alignment==0||p.alignment==1) && autoTurnOk;
}
inline std::string readerPreferencesFile() {
  const char* root=std::getenv("BOOKS_STATE_DIR");
  return std::string(root&&*root?root:"/mnt/mmc/.books")+"/reading-settings.conf";
}
inline bool loadReaderPreferences(ReaderPreferences& value) {
  FILE* f=std::fopen(readerPreferencesFile().c_str(),"rb");
  if(!f)return false;
  char magic[32]{};
  int margin=0,spacing=0,align=0,hyphen=0,css=0,night=0,focus=0,autoTurn=0;
  const int n=std::fscanf(f,"%31s %d %d %d %d %d %d %d %d",
       magic,&margin,&spacing,&align,&hyphen,&css,&night,&focus,&autoTurn);
  std::fclose(f);
  const bool v1=std::string(magic)=="BOOKS-SETTINGS1" && n==6;
  const bool v2=std::string(magic)=="BOOKS-SETTINGS2" && n==7;
  const bool v3=std::string(magic)=="BOOKS-SETTINGS3" && n==9;
  if(!v3){focus=0;autoTurn=0;}
  ReaderPreferences temp{margin,spacing,align,hyphen!=0,css!=0,night!=0,focus!=0,autoTurn};
  if((!v1&&!v2&&!v3)||hyphen<0||hyphen>1||css<0||css>1||
     night<0||night>1||focus<0||focus>1||!validReaderPreferences(temp))return false;
  value=temp;
  return true;
}
inline bool saveReaderPreferences(const ReaderPreferences& value) {
  if(!validReaderPreferences(value))return false;
  const std::string name=readerPreferencesFile(),tmp=name+".tmp";
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"BOOKS-SETTINGS3 %d %d %d %d %d %d %d %d\n",
    value.margin,value.linePercent,value.alignment,
    value.hyphenation?1:0,value.embeddedStyle?1:0,value.nightMode?1:0,
    value.focusReading?1:0,value.autoTurnSeconds)>0;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok&&std::rename(tmp.c_str(),name.c_str())==0)return true;
  std::remove(tmp.c_str());
  return false;
}
} // namespace books
