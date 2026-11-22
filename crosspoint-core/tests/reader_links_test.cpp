// SPDX-License-Identifier: MIT
#include "ReaderLinks.hpp"
#include <cassert>
#include <string>
int main() {
  const auto local=books::resolveInternalLink(
      "OEBPS/text/ch1.xhtml","#footnote-1");
  assert(local.valid&&local.path=="OEBPS/text/ch1.xhtml"&&
         local.anchor=="footnote-1");
  const auto relative=books::resolveInternalLink(
      "OEBPS/text/ch1.xhtml","../notes/notes.xhtml#note-2");
  assert(relative.valid&&relative.path=="OEBPS/notes/notes.xhtml" &&
         relative.anchor=="note-2");
  const auto plain=books::resolveInternalLink(
      "OEBPS/text/ch1.xhtml","ch2.xhtml");
  assert(plain.valid&&plain.path=="OEBPS/text/ch2.xhtml" &&
         plain.anchor.empty());
  for(const auto& bad:{"../../../../etc/passwd#evil","https://example.org",
                       "//example.com/a","C:evil","chapter.xhtml?query",
                       "../..\\evil"}) {
    assert(!books::resolveInternalLink("OEBPS/a.xhtml",bad).valid);
  }
  assert(!books::resolveInternalLink("OEBPS/a.xhtml","").valid);
  return 0;
}
