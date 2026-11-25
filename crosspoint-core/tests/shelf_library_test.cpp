// SPDX-License-Identifier: MIT
#include "ShelfHistory.hpp"
#include "ShelfSort.hpp"
#include "ShelfDetails.hpp"
#include "ShelfCover.hpp"
#include "ShelfFolders.hpp"
#include "ShelfLibrary.hpp"
#include "ShelfCollections.hpp"
#include "ShelfReadingStatus.hpp"
#include <chrono>
#include <sys/stat.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <unistd.h>

struct TestBook {
  std::string path;
  std::string label;
  long modified;
};
int main() {
  unsigned long long bytes=0,modified=0;
  assert(books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 4 6\n","BOOKS-SHELF1",bytes,modified));
  assert(bytes==4 && modified==6);
  assert(!books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 184467440737095516160 6\n",
      "BOOKS-SHELF1",bytes,modified));
  assert(!books::parseShelfIdentityHeader(
      "BOOKS-SHELF1 -1 2\n","BOOKS-SHELF1",bytes,modified));
  char dir[]="/tmp/books-shelf-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string a=std::string(dir)+"/A story.epub";
  const std::string b=std::string(dir)+"/B story.epub";
  for(const auto& path:{a,b}) {
    FILE* f=std::fopen(path.c_str(),"wb");
    assert(f);
    assert(std::fputs("book",f)>=0);
    assert(std::fclose(f)==0);
  }
  using Status=books::ReadingStatus;
  assert(books::shelfReadingStatus(a,{})==Status::Unread);
  assert(books::shelfReadingStatus(a,{a})==Status::Reading);
  assert(books::shelfReadingStatus(a+".missing",{a+".missing"})==Status::Unread);
  assert(books::saveReadingPosition(a,{0,0,'?',0,1000000}));
  assert(books::shelfReadingStatus(a,{})==Status::Reading);
  assert(books::saveShelfProgress(a,0));
  assert(books::shelfReadingStatus(a,{})==Status::Reading);
  assert(books::saveShelfProgress(a,999999));
  assert(books::shelfReadingStatus(a,{})==Status::Reading);
  assert(books::saveShelfProgress(a,1000000));
  assert(books::shelfReadingStatus(a,{})==Status::Completed);
  assert(books::saveShelfProgress(a,500000));
  assert(books::shelfReadingStatus(a,{})==Status::Reading);
  assert(books::classifyReadingStatus(true,1000001,false,false)==Status::Unread);
  assert(books::loadRecentBooks().empty());
  assert(books::recordRecentBook(a));
  assert(books::recordRecentBook(b));
  assert(books::recordRecentBook(a));
  auto recent=books::loadRecentBooks();
  assert(recent.size()==2 && recent[0]==a && recent[1]==b);
  std::vector<TestBook> books={{b,"zebra",30},{a,"Alpha",20}};
  books::sortShelfBooks(books,books::ShelfSortMode::Title,recent);
  assert(books[0].path==a);
  books::sortShelfBooks(books,books::ShelfSortMode::RecentlyAdded,recent);
  assert(books[0].path==b);
  books::sortShelfBooks(books,books::ShelfSortMode::Recent,recent);
  assert(books[0].path==a);
  assert(books::lastOpenedIndex(books,recent)==0);
  assert(books::saveShelfDetails(a,{u8"Tiếng Việt",u8"Nguyễn Văn A"}));
  books::ShelfDetails details;
  assert(books::loadShelfDetails(a,details));
  assert(details.title==u8"Tiếng Việt" && details.author==u8"Nguyễn Văn A");
  books::PngPreview picture;
  picture.width=2;
  picture.height=2;
  picture.gray={0,255,255,0};
  books::ShelfCover cover,loadedCover;
  assert(books::makeShelfCover(picture,cover));
  assert(books::saveShelfCover(a,cover));
  assert(books::loadShelfCover(a,loadedCover));
  assert(cover.ink==loadedCover.ink);
  // Overwriting an EPUB must invalidate cached title/author metadata.
  FILE* f=std::fopen(a.c_str(),"ab");
  assert(f);
  assert(std::fputs("changed",f)>=0);
  assert(std::fclose(f)==0);
  assert(!books::loadShelfDetails(a,details));
  assert(!books::loadShelfCover(a,loadedCover));
  assert(books::shelfReadingStatus(a,{})==Status::Unread);
  // Ignore malformed history rather than trusting arbitrary SD bytes.
  f=std::fopen(books::recentBooksFile().c_str(),"wb");
  assert(f);
  assert(std::fputs("BOOKS-RECENT1\nnot-hex\n",f)>=0);
  assert(std::fclose(f)==0);
  assert(books::loadRecentBooks().empty());
  const auto renamed=std::string(dir)+"/Renamed.epub";
  books::Collections tags;
  tags.favorites.insert(a);
  tags.groups["Fiction"].insert(a);
  tags.groups["On vacation"].insert(a);
  tags.identities[a]=books::collectionIdentity(a);
  assert(!tags.nameAvailable("fICTion"));
  assert(books::saveCollections(tags));
  assert(std::rename(a.c_str(),renamed.c_str())==0);
  tags=books::loadCollections();
  assert(books::restoreMovedCollectionBooks(tags,books::scanCardBooks(dir)));
  assert(tags.favorites.count(renamed)==1 && tags.groups["Fiction"].count(renamed)==1 &&
         tags.groups["On vacation"].count(renamed)==1);
  tags.groups.erase("Fiction");
  assert(books::saveCollections(tags));
  assert(books::loadCollections().favorites.count(renamed)==1);
  assert(std::remove(books::collectionsFile().c_str())==0);
  assert(std::remove(renamed.c_str())==0);
  assert(std::remove(b.c_str())==0);
  // M4 folders: built from the book paths, empty folders hidden.
  {
    const std::string r="/mnt/mmc/Books";
    std::vector<TestBook> shelf={{r+"/Top.epub","Top",1},{r+"/Fiction/Sci-fi/Dune.epub","Dune",1},
                                 {r+"/Fiction/Emma.epub","Emma",1},{r+"/fiction2/a.txt","a",1},
                                 {r+"/Fiction/Sci-fi/Deep/x.epub","x",1},{"/elsewhere/y.epub","y",1}};
    auto top=books::shelfFolderView(shelf,r,"");
    assert((top.folders==std::vector<std::string>{"Fiction","fiction2"}) && top.books==std::vector<size_t>{0});
    auto fic=books::shelfFolderView(shelf,r,"Fiction");
    assert(fic.folders==std::vector<std::string>{"Sci-fi"} && fic.books==std::vector<size_t>{2});
    auto sci=books::shelfFolderView(shelf,r,"Fiction/Sci-fi");
    assert(sci.folders==std::vector<std::string>{"Deep"} && sci.books==std::vector<size_t>{1});
    assert(books::shelfFolderView(shelf,r,"Fict").books.empty());  // not a prefix match
    assert(books::shelfParentFolder("Fiction/Sci-fi")=="Fiction" && books::shelfParentFolder("Fiction").empty());
    assert(books::shelfBreadcrumb("")=="Books");
    assert(books::shelfBreadcrumb("Fiction/Sci-fi")=="Books > Fiction > Sci-fi");
    assert(books::shelfBreadcrumb("Fiction/Sci-fi/Deep/Deeper",24)=="... > Deep > Deeper");
    std::string where;
    assert(!books::shelfRelativeFolder(r,"/mnt/mmc/Books2/x.epub",where));
  }
  // The same groups order filenames and drive quick jumps.
  using books::alphabet::titleGroup;
  assert(titleGroup("Ắ") == titleGroup("A\xcc\x86\xcc\x81"));
  assert(titleGroup("Đà Lạt") == titleGroup("D"));
  assert(titleGroup("École") == titleGroup(" ecole"));
  assert(books::compareShelfNames("école", "E\xcc\x81" "COLE") == 0);
  assert(books::compareShelfNames("Écho", "Fable") < 0);
  assert(books::alphabet::label(titleGroup("１２３")) == "0-9");
  assert(books::alphabet::label(titleGroup("۱۲۳")) == "0-9");
  assert(books::alphabet::label(titleGroup("📚 Books")) == "#");
  assert(books::alphabet::label(titleGroup("漢字")) == "Han");
  assert(books::alphabet::label(titleGroup("かな")) == "Kana");
  assert(books::alphabet::label(titleGroup("한글")) == "Hangul");
  // M4 large library: 4000 books in 400 folders, scanned, sorted and shown
  // folder by folder well within a second on the host.
  {
    const std::string root=std::string(dir)+"/Big";
    assert(::mkdir(root.c_str(),0755)==0);
    for(int folder=0;folder<400;++folder) {
      const std::string sub=root+"/Author "+std::to_string(folder%40)+(folder<40?"":"/Series "+std::to_string(folder));
      ::mkdir((root+"/Author "+std::to_string(folder%40)).c_str(),0755);
      ::mkdir(sub.c_str(),0755);
      for(int i=0;i<10;++i) {
        FILE* book=std::fopen((sub+"/Book "+std::to_string(folder*10+i)+(i%3?".epub":".txt")).c_str(),"wb");
        assert(book && std::fclose(book)==0);
      }
    }
    const auto start=std::chrono::steady_clock::now();
    auto all=books::scanCardBooks(root);
    books::sortShelfBooks(all,books::ShelfSortMode::Folders,{});
    size_t seen=0;
    auto view=books::shelfFolderView(all,root,"");
    for(const auto& author:view.folders) {
      auto inside=books::shelfFolderView(all,root,author);
      seen+=inside.books.size();
      for(const auto& series:inside.folders)
        seen+=books::shelfFolderView(all,root,author+"/"+series).books.size();
    }
    const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now()-start).count();
    std::printf("4000 books: scan, sort and 401 folder views in %lld ms\n",static_cast<long long>(ms));
    assert(all.size()==4000 && seen==4000 && view.folders.size()==40);
    assert(all.front().label=="Book 0" && all[1].label=="Book 1");  // .txt and .epub both lose their extension
    assert(ms<5000);
    assert(std::system(("rm -rf '"+root+"'").c_str())==0);
  }
  std::puts("Books shelf library, folders and large library: PASS");
  return 0;
}
