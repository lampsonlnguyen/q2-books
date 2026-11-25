// SPDX-License-Identifier: MIT
#pragma once
#include "ShelfProgress.hpp"
#include "ShelfHistory.hpp"

namespace books {
enum class ReadingStatus {Unread, Reading, Completed};
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
  uint32_t ppm=0;
  const bool known=loadShelfProgress(path,ppm);
  if(known)return classifyReadingStatus(true,ppm,false,false);
  ReadingPosition position;
  return classifyReadingStatus(false,0,loadReadingPosition(path,position),
      std::find(recent.begin(),recent.end(),path)!=recent.end());
}
} // namespace books
