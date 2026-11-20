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
struct ReadingPosition { int spine=0; size_t page=0; };

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
  unsigned long long bytes=0,modified=0,page=0;
  int spine=-1;
  const int parsed=std::fscanf(f,"CROSSPOD1 %llu %llu %d %llu",
                               &bytes,&modified,&spine,&page);
  std::fclose(f);
  if(parsed!=4 || spine<0 || spine>100000 ||
     page>1000000ULL ||
     bytes!=static_cast<unsigned long long>(st.st_size) ||
     modified!=static_cast<unsigned long long>(st.st_mtime))return false;
  out={spine,static_cast<size_t>(page)};
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
  bool ok=std::fprintf(f,"CROSSPOD1 %llu %llu %d %llu\n",
      static_cast<unsigned long long>(st.st_size),
      static_cast<unsigned long long>(st.st_mtime),
      position.spine,static_cast<unsigned long long>(position.page))>0;
  if(std::fflush(f)!=0)ok=false;
  if(ok && fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),file.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace crosspod
