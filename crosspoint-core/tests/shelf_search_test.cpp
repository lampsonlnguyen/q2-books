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
  char temp[]="/tmp/books-search-XXXXXX";
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
  put(root+"/fifth.txt");   // RD-04: plain text is a book too
  put(root+"/notes.pdf");   // other types are not
  put(root+"/sixth.txt.bak");
  assert(symlink(c.c_str(),(root+"/linked").c_str())==0);
  const auto books=books::scanCardBooks(root);
  assert(books.size()==5);
  const auto limited=books::scanCardBooks(
      root,books::ShelfScanLimits{512,256,1});
  assert(limited.size()==3);
  const auto capped=books::scanCardBooks(
      root,books::ShelfScanLimits{1,256,5});
  assert(capped.size()==1);
  assert(books::isEpubFilename("test.ePuB"));
  assert(!books::isEpubFilename("test.epub.bak"));
  assert(books::shelfSearchMatches(
      "alpha","ALPHA","",""));
  assert(books::shelfSearchMatches(
      u8"Tiếng",u8"Tie\u0302\u0301ng Việt.epub","",""));
  assert(books::shelfSearchMatches(
      "tolkien","","The Hobbit","J.R.R. Tolkien"));
  assert(!books::shelfSearchMatches(
      "tolkien","Alpha","Chapter 1","Unknown"));
  return 0;
}
