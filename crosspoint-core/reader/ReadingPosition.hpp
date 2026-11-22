// SPDX-License-Identifier: MIT
#pragma once
// Small POSIX adapter for CrossPod's per-book reading position.
// Does not replace the upstream CrossPoint page or metadata engine.
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace crosspod {
// page alone names content only for the engine and typography that laid it out.
// So the original engine also stores the page's exact visible-text offset, and
// either engine stores its place as a fraction of the chapter (parts per
// million of its pages), which maps a position across an engine fallback.
// engine: 'o' original CrossPoint Section, 'l' legacy collector, '?' unknown
// (a CROSSPOD1 file, which held only the page).
struct ReadingPosition {
  int spine=0; size_t page=0;
  char engine='?'; uint32_t textOffset=0; uint32_t fractionPpm=0;
};

inline std::string positionFile(const std::string& book) {
  const char* overrideDir=std::getenv("CROSSPOD_STATE_DIR");
  const std::string dir=(overrideDir && *overrideDir)?overrideDir:"/mnt/mmc/.crosspod";
  // FNV-1a over the complete path, not the potentially duplicate book title.
  uint64_t hash=14695981039346656037ULL;
  for (unsigned char byte:book) { hash^=byte; hash*=1099511628211ULL; }
  char name[48];
  std::snprintf(name,sizeof(name),"/progress-%016llx",
                static_cast<unsigned long long>(hash));
  return dir+name;
}

inline bool loadReadingPosition(const std::string& book,ReadingPosition& out) {
  struct stat st{};
  if(stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  std::FILE* f=std::fopen(positionFile(book).c_str(),"rb");
  if(!f)return false;
  unsigned long long bytes=0,modified=0,page=0,offset=0,ppm=0;
  int spine=-1;
  char engine='?';
  char line[160]{};
  const bool read=std::fgets(line,sizeof(line),f)!=nullptr;
  std::fclose(f);
  if(!read)return false;
  int parsed=std::sscanf(line,"CROSSPOD2 %llu %llu %d %llu %c %llu %llu",
                         &bytes,&modified,&spine,&page,&engine,&offset,&ppm);
  if(parsed!=7) {
    engine='?';offset=0;ppm=0;
    parsed=std::sscanf(line,"CROSSPOD1 %llu %llu %d %llu",&bytes,&modified,&spine,&page)==4?7:0;
  }
  if(parsed!=7 || spine<0 || spine>100000 ||
     page>1000000ULL || offset>0xffffffffULL || ppm>1000000ULL ||
     (engine!='o' && engine!='l' && engine!='?') ||
     bytes!=static_cast<unsigned long long>(st.st_size) ||
     modified!=static_cast<unsigned long long>(st.st_mtime))return false;
  out={spine,static_cast<size_t>(page),engine,static_cast<uint32_t>(offset),
       static_cast<uint32_t>(ppm)};
  return true;
}

inline bool saveReadingPosition(const std::string& book,ReadingPosition position) {
  if(position.spine<0 || position.page>1000000)return false;
  struct stat st{};
  if(stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  const std::string file=positionFile(book);
  const std::string temp=file+".tmp";
  std::FILE* f=std::fopen(temp.c_str(),"wb");
  if(!f)return false;
  if(position.engine!='o' && position.engine!='l')position.engine='?';
  bool ok=std::fprintf(f,"CROSSPOD2 %llu %llu %d %llu %c %lu %lu\n",
      static_cast<unsigned long long>(st.st_size),
      static_cast<unsigned long long>(st.st_mtime),
      position.spine,static_cast<unsigned long long>(position.page),position.engine,
      static_cast<unsigned long>(position.textOffset),
      static_cast<unsigned long>(position.fractionPpm>1000000?1000000:position.fractionPpm))>0;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),file.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace crosspod
