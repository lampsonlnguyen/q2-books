// SPDX-License-Identifier: MIT
#pragma once
#include "ShelfProgress.hpp"
#include "ShelfHistory.hpp"
#include <optional>
#include <cerrno>

namespace books {
enum class ReadingStatus {Unread, Reading, Completed};
enum class ReadingStatusFilter {All, Unread, Reading, Completed};
enum class ReadingStatusOverride {Automatic, Unread, Completed};

inline const char* readingStatusLabel(ReadingStatusFilter filter) {
  switch(filter) {
    case ReadingStatusFilter::Unread:return "Unread";
    case ReadingStatusFilter::Reading:return "Reading";
    case ReadingStatusFilter::Completed:return "Completed";
    default:return "All";
  }
}
inline bool matchesReadingStatus(ReadingStatusFilter filter,ReadingStatus status) {
  return filter==ReadingStatusFilter::All ||
      static_cast<int>(filter)==static_cast<int>(status)+1;
}
inline bool writeStatusFile(const std::string& path,const std::string& text) {
  std::string temp=path+".tmp.XXXXXX";
  const int fd=::mkstemp(temp.data());
  if(fd<0)return false;
  FILE* f=::fdopen(fd,"wb");
  if(!f){::close(fd);::unlink(temp.c_str());return false;}
  bool ok=std::fwrite(text.data(),1,text.size(),f)==text.size() &&
      std::fflush(f)==0 && ::fsync(fd)==0;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),path.c_str())==0)return true;
  ::unlink(temp.c_str());return false;
}
inline std::string readingStatusFilterFile() {
  const auto path=recentBooksFile();
  return path.substr(0,path.find_last_of('/')+1)+"reading-status-filter.conf";
}
inline ReadingStatusFilter loadReadingStatusFilter() {
  FILE* f=std::fopen(readingStatusFilterFile().c_str(),"rb");
  if(!f)return ReadingStatusFilter::All;
  char text[32]{};
  const bool ok=std::fgets(text,sizeof(text),f) && std::fgetc(f)==EOF && !std::ferror(f);
  std::fclose(f);
  const std::string line(text);
  if(ok && line.size()==16 && line.compare(0,14,"BOOKS-STATUS1 ")==0 &&
     line[14]>='0' && line[14]<='3' && line[15]=='\n')
    return static_cast<ReadingStatusFilter>(line[14]-'0');
  return ReadingStatusFilter::All;
}
inline bool saveReadingStatusFilter(ReadingStatusFilter filter) {
  const int value=static_cast<int>(filter);
  return value>=0 && value<=3 &&
      writeStatusFile(readingStatusFilterFile(),"BOOKS-STATUS1 "+std::to_string(value)+"\n");
}
inline ReadingStatusOverride loadReadingStatusOverride(const std::string& book) {
  struct stat st{};
  if(::stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return ReadingStatusOverride::Automatic;
  FILE* f=std::fopen((positionFile(book)+".status").c_str(),"rb");
  if(!f)return ReadingStatusOverride::Automatic;
  char header[128]{},value[8]{};
  unsigned long long bytes=0,modified=0;
  const bool ok=std::fgets(header,sizeof(header),f) &&
      parseShelfIdentityHeader(header,"BOOKS-STATUS1",bytes,modified) &&
      bytes==static_cast<unsigned long long>(st.st_size) &&
      modified==static_cast<unsigned long long>(st.st_mtime) &&
      std::fgets(value,sizeof(value),f) && std::fgetc(f)==EOF && !std::ferror(f);
  std::fclose(f);
  if(ok && std::string(value)=="1\n")return ReadingStatusOverride::Unread;
  if(ok && std::string(value)=="2\n")return ReadingStatusOverride::Completed;
  return ReadingStatusOverride::Automatic;
}
inline bool saveReadingStatusOverride(const std::string& book,ReadingStatusOverride value) {
  const auto path=positionFile(book)+".status";
  if(value==ReadingStatusOverride::Automatic)
    return ::unlink(path.c_str())==0 || errno==ENOENT;
  struct stat st{};
  if((value!=ReadingStatusOverride::Unread && value!=ReadingStatusOverride::Completed) ||
     ::stat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  return writeStatusFile(path,"BOOKS-STATUS1 "+std::to_string(static_cast<unsigned long long>(st.st_size))+
      " "+std::to_string(static_cast<unsigned long long>(st.st_mtime))+"\n"+
      std::to_string(static_cast<int>(value))+"\n");
}
inline constexpr uint32_t completedBookPpm=1000000u;

// Only a valid whole-book percentage may establish automatic completion.
// Chapter fractions and legacy BOOKS1 positions are never completion evidence.
// A valid zero-percent position or a recent successful open still means Reading.
inline ReadingStatus classifyReadingStatus(bool hasProgress,uint32_t ppm,
                                           bool hasPosition,bool inHistory) {
  if(hasProgress && ppm==completedBookPpm)return ReadingStatus::Completed;
  if((hasProgress && ppm<=completedBookPpm) || hasPosition || inHistory)
    return ReadingStatus::Reading;
  return ReadingStatus::Unread;
}

// Callers should load history once per library scan, not once per book.
inline ReadingStatus shelfReadingStatus(const std::string& path,
                                        const std::vector<std::string>& recent) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return ReadingStatus::Unread;
  const auto manual=loadReadingStatusOverride(path);
  if(manual==ReadingStatusOverride::Unread)return ReadingStatus::Unread;
  if(manual==ReadingStatusOverride::Completed)return ReadingStatus::Completed;
  uint32_t ppm=0;
  const bool known=loadShelfProgress(path,ppm);
  if(known)return classifyReadingStatus(true,ppm,false,false);
  ReadingPosition position;
  return classifyReadingStatus(false,0,loadReadingPosition(path,position),
      std::find(recent.begin(),recent.end(),path)!=recent.end());
}
} // namespace books
