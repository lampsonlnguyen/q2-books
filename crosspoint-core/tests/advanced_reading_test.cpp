// SPDX-License-Identifier: MIT
#include "Highlights.hpp"
#include "OfflineDictionary.hpp"
#include "FootnotePreview.hpp"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main() {
  char path[]="/tmp/books-advanced-XXXXXX";
  assert(mkdtemp(path));
  assert(setenv("BOOKS_STATE_DIR",path,1)==0);
  const std::string book=std::string(path)+"/unicode.epub";
  FILE* file=std::fopen(book.c_str(),"wb");
  assert(file);
  assert(std::fputs("epub content",file)>0);
  assert(std::fclose(file)==0);
  assert(books::addHighlight(book,{2,100,110}));
  assert(books::addHighlight(book,{2,105,120}));
  assert(books::addHighlight(book,{2,120,130}));
  auto entries=books::loadHighlights(book);
  assert(entries.size()==1);
  assert(entries[0].spine==2 && entries[0].begin==100 &&
         entries[0].end==130);
  assert(books::addHighlight(book,{4,500,510}));
  entries=books::loadHighlights(book);
  assert(entries.size()==2);
  assert(books::removeHighlightAt(book,2,108));
  entries=books::loadHighlights(book);
  assert(entries.size()==1 && entries[0].spine==4);
  assert(!books::removeHighlightAt(book,2,108));
  assert(!books::addHighlight(book,{3,10,10}));
  assert(!books::addHighlight(book,{3,11,9}));
  FILE* dict=std::fopen(books::dictionaryFile().c_str(),"wb");
  assert(dict);
  assert(std::fputs("book\tA collection of pages.\n"
                    "sách\tVietnamese: book.\n",dict)>0);
  assert(std::fclose(dict)==0);
  assert(books::lookupDefinition("BOOK!")=="A collection of pages.");
  assert(books::lookupDefinition("sách")=="Vietnamese: book.");
  assert(books::lookupDefinition("missing").empty());
  const std::string xhtml=R"xml(<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
<p id="plain-target">Outside text</p>
<aside epub:type="footnote" xmlns:epub="http://www.idpf.org/2007/ops"
 id="note-1"><p>Note with <em>nested</em> content &amp; entities.</p></aside>
</body></html>)xml";
  assert(books::extractFootnoteText(xhtml,"note-1")==
         "Note with nested content & entities.");
  assert(books::extractFootnoteText(xhtml,"missing").empty());
  assert(books::extractFootnoteText(xhtml,"plain-target").empty());
  assert(books::extractFootnoteText(
         "<html><aside id=\"note-1\">broken","note-1").empty());
  struct stat st{};
  assert(::stat(book.c_str(),&st)==0);
  // Cache identity rejects an EPUB that has changed since selection.
  FILE* append=std::fopen(book.c_str(),"ab");
  assert(append);
  assert(std::fputs("different",append)>0);
  assert(std::fclose(append)==0);
  assert(books::loadHighlights(book).empty());
  return 0;
}
