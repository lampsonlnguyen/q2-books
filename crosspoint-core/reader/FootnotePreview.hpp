// SPDX-License-Identifier: MIT
#pragma once
// Bounded EPUB footnote preview. Reuses the original EPUB ZIP and Expat XML
// parser; no HTML-to-page reflow, network I/O or ESP32 firmware.
#include "ReaderLinks.hpp"
#include <ZipFile.h>
#include <expat.h>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <string>

namespace books {
struct FootnoteCapture {
  std::string anchor;
  std::string text;
  int depth=0;
  bool capturing=false;
  bool finished=false;
};
inline void XMLCALL footnoteStart(void* user,const XML_Char* name,
                                  const XML_Char** attrs) {
  auto& c=*static_cast<FootnoteCapture*>(user);
  if(c.finished)return;
  if(c.capturing) {++c.depth;return;}
  bool target=false,declaredNote=false;
  const std::string tag(name?name:"");
  // CrossPoint's Page::footnotes also contains ordinary <a href> links.
  // Only actual semantic note elements should open an excerpt popup.
  const bool aside=tag=="aside" ||
      (tag.size()>6 && tag.compare(tag.size()-6,6,":aside")==0);
  for(size_t i=0;attrs && attrs[i] && attrs[i+1];i+=2) {
    const std::string key(attrs[i]),value(attrs[i+1]);
    if((key=="id"||key=="xml:id") && c.anchor==value)target=true;
    if((key=="epub:type" && (value.find("footnote")!=std::string::npos ||
                              value.find("endnote")!=std::string::npos)) ||
       (key=="role" && (value.find("doc-footnote")!=std::string::npos ||
                         value.find("doc-endnote")!=std::string::npos)))
      declaredNote=true;
  }
  if(target && (aside||declaredNote)) {
    c.capturing=true;c.depth=1;
  }
}
inline void XMLCALL footnoteEnd(void* user,const XML_Char*) {
  auto& c=*static_cast<FootnoteCapture*>(user);
  if(c.capturing && --c.depth==0) {
    c.capturing=false;c.finished=true;
  }
}
inline void XMLCALL footnoteText(void* user,const XML_Char* text,int bytes) {
  auto& c=*static_cast<FootnoteCapture*>(user);
  if(!c.capturing||bytes<=0||c.text.size()>=1500)return;
  c.text.append(text,static_cast<size_t>(std::min<int>(
      bytes,static_cast<int>(1500-c.text.size()))));
}
inline std::string extractFootnoteText(const std::string& xhtml,
                                      const std::string& anchor) {
  if(xhtml.empty() || xhtml.size()>256*1024 || anchor.empty()||
     anchor.size()>180)return {};
  XML_Parser parser=XML_ParserCreate(nullptr);
  if(!parser)return {};
  FootnoteCapture c;
  c.anchor=anchor;
  XML_SetUserData(parser,&c);
  XML_SetElementHandler(parser,footnoteStart,footnoteEnd);
  XML_SetCharacterDataHandler(parser,footnoteText);
  const bool okay=XML_Parse(parser,xhtml.data(),static_cast<int>(xhtml.size()),1)
      ==XML_STATUS_OK;
  XML_ParserFree(parser);
  if(!okay || !c.finished)return {};
  // XML text callback has already expanded entities; normalize whitespace.
  std::string result;
  result.reserve(c.text.size());
  bool previousSpace=false;
  for(unsigned char ch:c.text) {
    const bool whitespace=ch==' '||ch=='\n'||ch=='\r'||ch=='\t';
    if(whitespace) {
      if(!previousSpace && !result.empty())result+=' ';
    } else result+=static_cast<char>(ch);
    previousSpace=whitespace;
  }
  while(!result.empty() && result.back()==' ')result.pop_back();
  return result;
}
inline std::string loadFootnotePreview(ZipFile& zip,
                                       const std::string& currentPath,
                                       const std::string& href) {
  const auto target=resolveInternalLink(currentPath,href);
  if(!target.valid||target.anchor.empty())return {};
  size_t size=0;
  if(!zip.getInflatedFileSize(target.path.c_str(),&size) ||
     size==0||size>256*1024)return {};
  std::unique_ptr<uint8_t,decltype(&std::free)> raw(
      zip.readFileToMemory(target.path.c_str(),&size,false),&std::free);
  if(!raw || size==0||size>256*1024)return {};
  return extractFootnoteText(std::string(
      reinterpret_cast<const char*>(raw.get()),size),target.anchor);
}
} // namespace books
