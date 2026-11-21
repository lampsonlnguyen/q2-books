// SPDX-License-Identifier: MIT
#include "ShelfLibrary.hpp"
#include "ShelfSearch.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  char temp[]="/tmp/crosspod-search-XXXXXX";
  assert(mkdtemp(temp));
  const std::string root=temp;
  const std::string a=root+"/Classics", b=a+"/Vietnam", c=b+"/Translations";
  assert(mkdir(a.c_str(),0700)==0);
  assert(mkdir(b.c_str(),0700)==0);
  assert(mkdir(c.c_str(),0700)==0);
  const auto put=[](const std::string& path) {
    FILE* f=std::fopen(path.c_str(),"wb");
    assert(f);
    assert(std::fputs("epub",f)>=0);
    assert(std::fclose(f)==0);
  };
  put(root+"/ALPHA.EPUB");
  put(a+"/Second.Epub");
  put(b+"/Tie\u0302\u0301ng Vie\u0323t.epub");
  put(c+"/FOUR.epub");
  put(root+"/.hidden.epub");
  put(root+"/fifth.txt");
  assert(symlink(c.c_str(),(root+"/linked").c_str())==0);
  const auto books=crosspod::scanCardBooks(root);
  assert(books.size()==4);
  const auto limited=crosspod::scanCardBooks(
      root,crosspod::ShelfScanLimits{512,256,1});
  assert(limited.size()==2);
  const auto capped=crosspod::scanCardBooks(
      root,crosspod::ShelfScanLimits{1,256,5});
  assert(capped.size()==1);
  assert(crosspod::isEpubFilename("test.ePuB"));
  assert(!crosspod::isEpubFilename("test.epub.bak"));
  assert(crosspod::shelfSearchMatches(
      "alpha","ALPHA","",""));
  assert(crosspod::shelfSearchMatches(
      u8"Tiếng",u8"Tie\u0302\u0301ng Việt.epub","",""));
  assert(crosspod::shelfSearchMatches(
      "tolkien","","The Hobbit","J.R.R. Tolkien"));
  assert(!crosspod::shelfSearchMatches(
      "tolkien","Alpha","Chapter 1","Unknown"));
  return 0;
}
