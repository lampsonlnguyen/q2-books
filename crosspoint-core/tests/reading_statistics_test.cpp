// SPDX-License-Identifier: MIT
#include "ReadingStatistics.hpp"
#include <cassert>
#include <filesystem>

using namespace std::chrono;
using namespace books;

int main() {
  char dir[]="/tmp/books-statistics-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  assert(setenv("TZ","UTC0",1)==0);
  tzset();

  const std::string id="0123456789abcdef",title="Tiếng Việt \"test\"";
  const ReadingDay feb28=year(2028)/2/28, feb29=feb28+days(1), march1=feb29+days(1);
  const time_t midnight=duration_cast<seconds>(feb29.time_since_epoch()).count();
  ReadingStatistics stats;
  ReadingSession session;
  assert((stats.streaks(feb28)==std::pair{0u,0u}));
  session.observe(stats,id,title,{10,10,midnight-2},true,0,0);
  session.observe(stats,id,title,{13,13,midnight+1},true,0,0);
  assert(session.readPage());
  assert(stats.totals(feb28).seconds==2 && stats.totals(feb29).seconds==1);

  // Menus, dark panel, skipped event-loop intervals and actual suspend
  // discard elapsed time. A clock jump requires a new manual interaction.
  session.observe(stats,id,title,{14,14,midnight+2},false,0,0);
  session.observe(stats,id,title,{15,15,midnight+3},true,0,0);
  session.observe(stats,id,title,{16,80,midnight+68},true,0,0);
  assert(!session.readPage() && stats.totals(feb29).seconds==1);
  session.observe(stats,id,title,{17,81,midnight+3600},true,0,0);
  session.observe(stats,id,title,{18,82,midnight+3601},true,0,0);
  assert(stats.totals(feb29).seconds==1);
  session.interact();
  for(int i=1;i<=122;++i)
    session.observe(stats,id,title,{18+i,82+i,midnight+3601+i},true,0,0);
  assert(stats.totals(feb29).seconds==121 && !session.readPage());
  session.interact();
  session.observe(stats,id,title,{141,205,midnight+3724},true,0,0);
  session.observe(stats,id,title,{142,206,midnight+3725},true,0,0);
  assert(session.readPage());
  session.observe(stats,id,title,{142,206,midnight+3725},true,0,1);
  assert(!session.readPage());

  // A previously read range stays counted once even after page boundaries
  // change. Reading beyond it or filling a gap earns credit.
  stats.finishPage(feb28,id,title,0,0,0,200,false);
  stats.finishPage(feb29,id,title,0,0,40,100,false);
  stats.finishPage(feb29,id,title,0,0,400,500,false);
  stats.finishPage(feb29,id,title,0,0,190,420,false);
  stats.finishPage(feb29,id,title,0,0,0,500,false);
  stats.finishPage(march1,id,title,2,12,0,1,true);
  stats.finishPage(march1,id,title,2,13,0,1,true);
  assert(stats.totals(feb28).pages==1);
  assert(stats.totals(feb29).pages==2);
  assert(stats.totals(march1,7).pages==4 && stats.totals(march1).completed==1);

  assert((stats.streaks(march1)==std::pair{3u,3u}));
  assert((stats.streaks(march1+days(1))==std::pair{3u,3u}));
  assert((stats.streaks(march1+days(2))==std::pair{0u,3u}));
  assert((stats.streaks(feb28)==std::pair{1u,1u}));  // clock set back

  const auto start=steady_clock::time_point(seconds(500));
  stats.startSession(start);
  assert(stats.save(false,start+seconds(59)));
  assert(ReadingStatistics().books().empty());
  assert(stats.save(false,start+seconds(60)));
  const auto path=std::filesystem::path(dir)/"reading-statistics";
  std::ofstream(path.string()+".tmp.interrupted")<<"BOOKS-STATS1\nD incomplete";
  ReadingStatistics reloaded;
  assert(reloaded.books().at(id).title==title);
  assert(reloaded.books().at(id).totals.completed==1);
  assert(reloaded.totals(march1,7).seconds==125);
  reloaded.finishPage(march1,id,title,0,0,100,490,false);
  assert(reloaded.totals(march1,7).pages==4);

  assert(ReadingGoals::load().minutes==0);
  assert((ReadingGoals{15,20}.save()));
  assert(ReadingGoals::load().minutes==15 && ReadingGoals::load().pages==20);
  assert((ReadingGoals{0,10}.save()));
  assert(ReadingGoals::load().minutes==0 && ReadingGoals::load().pages==10);
  assert((reloaded.streaks(march1)==std::pair{3u,3u}));

  assert(setenv("TZ","America/Los_Angeles",1)==0);
  tzset();
  assert(readingDay(midnight)==feb28);
  assert(readingDay(midnight+8*60*60)==feb29);

  std::ofstream(path)<<"BOOKS-STATS99\n";
  ReadingStatistics futureVersion;
  futureVersion.addTime(march1,id,title,1);
  assert(!futureVersion.save(true));
  std::string version;
  std::ifstream(path)>>version;
  assert(version=="BOOKS-STATS99");
  std::filesystem::remove_all(dir);
}
