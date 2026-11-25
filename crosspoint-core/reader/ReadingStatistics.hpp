// SPDX-License-Identifier: MIT
#pragma once

#include "ShelfReadingStatus.hpp"
#include "UiLanguage.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <map>
#include <sstream>
#include <vector>

namespace books {
using ReadingDay = std::chrono::sys_days;

inline ReadingDay readingDay(std::time_t now) {
  std::tm local{};
  if(!localtime_r(&now,&local))return {};
  using namespace std::chrono;
  return sys_days(year(local.tm_year+1900)/month(local.tm_mon+1)/day(local.tm_mday));
}

inline bool validReadingDay(ReadingDay day) {
  using namespace std::chrono;
  return day>=sys_days(year(2000)/1/1) && day<sys_days(year(2200)/1/1);
}

struct ReadingTotals {
  uint64_t seconds=0, pages=0, completed=0;
  ReadingTotals& operator+=(const ReadingTotals& other) {
    seconds+=other.seconds;pages+=other.pages;completed+=other.completed;
    return *this;
  }
};

struct BookReadingTotals {
  std::string title;
  ReadingTotals totals;
  // The text range is independent of typography. Empty/image-only pages and
  // the fallback renderer use page coordinates tied to the first layout seen.
  struct Coverage {
    uint64_t layout=0;
    std::vector<std::pair<uint32_t,uint32_t>> ranges;
  };
  std::map<int,Coverage> chapters;
};

struct ReadingGoals {
  unsigned minutes=0,pages=0;
  static ReadingGoals load() {
    ReadingGoals goals;
    std::ifstream f(uiStateDir()+"/reading-goals");
    char line[64]{};
    if(!f.getline(line,sizeof line) || f.peek()!=EOF)return {};
    std::istringstream row(line);
    std::string version,extra;
    if(row>>version>>goals.minutes>>goals.pages && version=="BOOKS-GOALS1" &&
       goals.minutes<=120 && goals.pages<=120 && !(row>>extra))return goals;
    return {};
  }
  bool save() const {
    return writeStatusFile(uiStateDir()+"/reading-goals",
        "BOOKS-GOALS1 "+std::to_string(minutes)+" "+std::to_string(pages)+"\n");
  }
};

class ReadingStatistics {
  static constexpr size_t maxBooks=4096, maxRanges=32768, maxFile=8*1024*1024;
  std::map<ReadingDay,ReadingTotals> days_;
  std::map<std::string,BookReadingTotals> books_;
  size_t rangeCount_=0;
  bool dirty_=false;
  bool writable_=true;
  using Clock=std::chrono::steady_clock;
  Clock::time_point lastSave_{};

  BookReadingTotals* book(const std::string& id,const std::string& title) {
    if(auto at=books_.find(id);at!=books_.end())return &at->second;
    if(books_.size()>=maxBooks)return nullptr;
    return &books_.emplace(id,BookReadingTotals{title,{},{}}).first->second;
  }

  static bool validTotals(const ReadingTotals& totals) {
    constexpr uint64_t maxSeconds=200ULL*366*24*60*60;
    return totals.seconds<=maxSeconds && totals.pages<=100000000 && totals.completed<=maxBooks;
  }

  bool read(std::istream& file) {
    std::string line;
    if(!std::getline(file,line) || line!="BOOKS-STATS1")return false;
    char kind;
    std::string current;
    while(std::getline(file,line)) {
      std::istringstream row(line);
      if(!(row>>kind))return false;
      if(kind=='D') {
        int day;
        ReadingTotals totals;
        if(!(row>>day>>totals.seconds>>totals.pages>>totals.completed) ||
           !validTotals(totals) || !validReadingDay(ReadingDay(std::chrono::days(day))) ||
           !days_.emplace(ReadingDay(std::chrono::days(day)),totals).second)return false;
      } else if(kind=='B') {
        BookReadingTotals info;
        if(!(row>>current>>info.totals.seconds>>info.totals.pages>>info.totals.completed>>
             std::quoted(info.title)) || current.size()!=16 || info.title.size()>240 ||
           !validTotals(info.totals) || info.totals.completed>1 ||
           books_.size()>=maxBooks || !books_.emplace(current,std::move(info)).second)return false;
      } else if(kind=='R') {
        int spine;
        uint64_t layout;
        uint32_t start,end;
        if(current.empty() || !(row>>spine>>layout>>start>>end) ||
           spine<0 || start>=end || ++rangeCount_>maxRanges)return false;
        auto& coverage=books_.at(current).chapters[spine];
        if(!coverage.ranges.empty() &&
           (coverage.layout!=layout || coverage.ranges.back().second>=start))return false;
        coverage.layout=layout;
        coverage.ranges.emplace_back(start,end);
      } else return false;
      if(row>>kind)return false;
    }
    return file.eof() && !file.bad();
  }

 public:
  ReadingStatistics() {
    const std::string path=uiStateDir()+"/reading-statistics";
    struct stat st{};
    if(::stat(path.c_str(),&st)!=0) {
      writable_=errno==ENOENT;
      return;
    }
    std::ifstream f(path);
    if(st.st_size<0 || static_cast<uint64_t>(st.st_size)>maxFile || !read(f)) {
      // Preserve unreadable or newer-version history for recovery.
      books_.clear();days_.clear();rangeCount_=0;writable_=false;
    }
  }

  const auto& books() const {return books_;}
  ReadingTotals totals(ReadingDay day,int count=1) const {
    ReadingTotals total;
    const auto start=day-std::chrono::days(count-1);
    for(auto it=days_.lower_bound(start);it!=days_.end() && it->first<=day;++it)
      total+=it->second;
    return total;
  }

  static bool qualifying(const ReadingTotals& total) {
    return total.seconds>=60 || total.pages>0;
  }

  std::pair<unsigned,unsigned> streaks(ReadingDay today) const {
    unsigned run=0,longest=0,current=0;
    ReadingDay previous{};
    for(const auto& [day,total]:days_) {
      if(day>today)break;
      if(!qualifying(total))continue;
      run=day==previous+std::chrono::days(1)?run+1:1;
      longest=std::max(longest,run);
      if(day==today || day+std::chrono::days(1)==today)current=run;
      previous=day;
    }
    return {current,longest};
  }

  void addTime(ReadingDay day,const std::string& id,const std::string& title,unsigned seconds) {
    if(!validReadingDay(day) || seconds==0)return;
    days_[day].seconds+=seconds;
    if(auto* entry=book(id,title))entry->totals.seconds+=seconds;
    dirty_=true;
  }

  void finishPage(ReadingDay day,const std::string& id,const std::string& title,int spine,
                  uint64_t layout,uint32_t start,uint32_t end,bool last) {
    if(!validReadingDay(day) || spine<0 || start>=end)return;
    auto* entry=book(id,title);
    if(!entry)return;
    if(last && entry->totals.completed==0) {
      ++entry->totals.completed;
      ++days_[day].completed;
      dirty_=true;
    }
    auto at=entry->chapters.find(spine);
    if(at==entry->chapters.end()) {
      if(rangeCount_>=maxRanges)return;
      at=entry->chapters.emplace(spine,BookReadingTotals::Coverage{layout,{}}).first;
    }
    auto& coverage=at->second;
    if(coverage.layout!=layout)return;
    auto& ranges=coverage.ranges;
    auto first=std::lower_bound(ranges.begin(),ranges.end(),start,
        [](const auto& range,uint32_t value){return range.second<value;});
    const bool covered=first!=ranges.end() && first->first<=start && first->second>=end;
    if(!covered) {
      auto after=first;
      while(after!=ranges.end() && after->first<=end) {
        start=std::min(start,after->first);
        end=std::max(end,after->second);
        ++after;
      }
      if(first==after && rangeCount_>=maxRanges)return;
      rangeCount_-=static_cast<size_t>(after-first);
      ranges.insert(ranges.erase(first,after),{start,end});
      ++rangeCount_;
      ++entry->totals.pages;
      ++days_[day].pages;
      dirty_=true;
    }
  }

  bool save(bool force=false,Clock::time_point now=Clock::now()) {
    if(!dirty_)return true;
    if(!writable_)return false;
    if(!force && now-lastSave_<std::chrono::minutes(1))return true;
    lastSave_=now;
    std::ostringstream out;
    out<<"BOOKS-STATS1\n";
    for(const auto& [day,total]:days_)
      out<<"D "<<day.time_since_epoch().count()<<' '<<total.seconds<<' '<<total.pages<<' '<<total.completed<<'\n';
    for(const auto& [id,book]:books_) {
      out<<"B "<<id<<' '<<book.totals.seconds<<' '<<book.totals.pages<<' '
         <<book.totals.completed<<' '<<std::quoted(book.title)<<'\n';
      for(const auto& [spine,coverage]:book.chapters)
        for(const auto& [start,end]:coverage.ranges)
          out<<"R "<<spine<<' '<<coverage.layout<<' '<<start<<' '<<end<<'\n';
    }
    if(!writeStatusFile(uiStateDir()+"/reading-statistics",out.str()))return false;
    dirty_=false;
    return true;
  }

  void startSession(Clock::time_point now=Clock::now()) {lastSave_=now;}
};

struct ReadingSample {
  int64_t steady,boot;
  std::time_t wall;
};
inline ReadingSample readingSample() {
  timespec boot{};
  const int64_t steady=std::chrono::duration_cast<std::chrono::seconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count();
  const int64_t sinceBoot=clock_gettime(CLOCK_BOOTTIME,&boot)==0?boot.tv_sec:steady;
  return {steady,sinceBoot,std::time(nullptr)};
}

class ReadingSession {
  static constexpr int idleSeconds=120;
  ReadingSample previous_{};
  int64_t activeUntil_=0;
  int spine_=-1;
  size_t page_=0;
  unsigned dwell_=0;
  bool visible_=false;

 public:
  void observe(ReadingStatistics& stats,const std::string& id,const std::string& title,
               ReadingSample now,bool visible,int spine,size_t page) {
    const int64_t elapsed=now.steady-previous_.steady;
    const int64_t wall=static_cast<int64_t>(now.wall)-previous_.wall;
    const int64_t boot=now.boot-previous_.boot;
    const bool continuous=elapsed>=0 && elapsed<=3 && std::abs(wall-elapsed)<=1 &&
        std::abs(boot-elapsed)<=1 && wall>=0;
    if(!visible || !continuous || !visible_ || spine_!=spine || page_!=page)dwell_=0;
    else {
      const auto eligible=std::clamp<int64_t>(activeUntil_-previous_.steady,0,elapsed);
      for(int64_t second=0;second<eligible;++second) {
        const auto day=readingDay(now.wall-std::min(wall,elapsed-second));
        stats.addTime(day,id,title,1);
        ++dwell_;
      }
      if(now.steady>=activeUntil_)dwell_=0;
    }
    if(visible && !visible_)activeUntil_=now.steady+idleSeconds;
    if(visible_ && !continuous)activeUntil_=now.steady;
    previous_=now;visible_=visible;spine_=spine;page_=page;
  }

  ReadingDay day() const {return readingDay(previous_.wall);}
  void interact() {if(visible_)activeUntil_=previous_.steady+idleSeconds;}
  bool readPage() const {return visible_ && dwell_>=2;}
};
}  // namespace books
