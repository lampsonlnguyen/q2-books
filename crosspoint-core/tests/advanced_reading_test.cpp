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
  char path[]="/tmp/crosspod-advanced-XXXXXX";
  assert(mkdtemp(path));
  assert(setenv("CROSSPOD_STATE_DIR",path,1)==0);
  const std::string book=std::string(path)+"/unicode.epub";
  FILE* file=std::fopen(book.c_str(),"wb");
  assert(file);
  assert(std::fputs("epub content",file)>0);
  assert(std::fclose(file)==0);
  assert(crosspod::addHighlight(book,{2,100,110}));
  assert(crosspod::addHighlight(book,{2,105,120}));
  assert(crosspod::addHighlight(book,{2,120,130}));
  auto entries=crosspod::loadHighlights(book);
  assert(entries.size()==1);
  assert(entries[0].spine==2 && entries[0].begin==100 &&
         entries[0].end==130);
  assert(crosspod::addHighlight(book,{4,500,510}));
  entries=crosspod::loadHighlights(book);
  assert(entries.size()==2);
  assert(crosspod::removeHighlightAt(book,2,108));
  entries=crosspod::loadHighlights(book);
  assert(entries.size()==1 && entries[0].spine==4);
  assert(!crosspod::removeHighlightAt(book,2,108));
  assert(!crosspod::addHighlight(book,{3,10,10}));
  assert(!crosspod::addHighlight(book,{3,11,9}));
  FILE* dict=std::fopen(crosspod::dictionaryFile().c_str(),"wb");
  assert(dict);
  assert(std::fputs("book\tA collection of pages.\n"
                    "sách\tVietnamese: book.\n",dict)>0);
  assert(std::fclose(dict)==0);
  assert(crosspod::lookupDefinition("BOOK!")=="A collection of pages.");
  assert(crosspod::lookupDefinition("sách")=="Vietnamese: book.");
  assert(crosspod::lookupDefinition("missing").empty());
  const std::string xhtml=R"xml(<?xml version="1.0"?>
<html xmlns="http://www.w3.org/1999/xhtml"><body>
<p>Outside text</p>
<aside id="note-1"><p>Note with <em>nested</em> content &amp; entities.</p></aside>
</body></html>)xml";
  assert(crosspod::extractFootnoteText(xhtml,"note-1")==
         "Note with nested content & entities.");
  assert(crosspod::extractFootnoteText(xhtml,"missing").empty());
  assert(crosspod::extractFootnoteText(
         "<html><aside id=\"note-1\">broken","note-1").empty());
  struct stat st{};
  assert(::stat(book.c_str(),&st)==0);
  // Cache identity rejects an EPUB that has changed since selection.
  FILE* append=std::fopen(book.c_str(),"ab");
  assert(append);
  assert(std::fputs("different",append)>0);
  assert(std::fclose(append)==0);
  assert(crosspod::loadHighlights(book).empty());
  return 0;
}
