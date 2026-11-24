// SPDX-License-Identifier: MIT
#pragma once
// Scan one never-opened EPUB's OPF and cover on demand, without allocating
// a CrossPoint Section, paginating a chapter, or changing the reader engine.
#include "ShelfDetails.hpp"
#include "ShelfCover.hpp"
#include "FlowCover.hpp"
#include "JpegPreview.hpp"
#include <ContainerParser.h>
#include <ContentOpfParser.h>
#include <ZipFile.h>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <sys/stat.h>

namespace books {
struct ShelfIndexResult {
  bool metadata=false;
  bool cover=false;
  bool updated=false;
  bool flowCover=false;
};
inline bool isSafeEpubItem(const std::string& item) {
  return !item.empty() && item.size()<1024 && item.front()!='/' &&
         item.find("..")==std::string::npos &&
         item.find('\\')==std::string::npos;
}
inline ShelfIndexResult indexShelfBook(const std::string& bookPath) {
  ShelfIndexResult result{};
  if(bookPath.empty() || bookPath.size()>1000)return result;
  if(isTxtFilename(bookPath))return result;  // no OPF or cover: the file name is the title
  struct stat st{};
  if(::lstat(bookPath.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size<=0 || st.st_size>128ll*1024*1024)return result;

  ShelfDetails existing{};
  const bool metadataReady=loadShelfDetails(bookPath,existing);
  ShelfCover cachedCover{};
  const bool coverReady=loadShelfCover(bookPath,cachedCover);
  FlowCover cachedFlow;
  const bool flowReady=loadFlowCover(bookPath,cachedFlow);
  result.flowCover=flowReady;
  result.metadata=metadataReady;
  result.cover=coverReady;
  if(metadataReady && coverReady && flowReady)return result;

  // Keep only ZIP metadata and at most one bounded image in memory.
  // ZipFile retains a const reference to bookPath for its entire lifetime.
  ZipFile zip(bookPath);
  if(!zip.loadAllFileStatSlims())return result;
  size_t bytes=0;
  if(!zip.getInflatedFileSize("META-INF/container.xml",&bytes) ||
     !bytes || bytes>256u*1024u)return result;
  ContainerParser container(bytes);
  if(!container.setup() ||
     !zip.readFileToStream("META-INF/container.xml",container,512) ||
     !isSafeEpubItem(container.fullPath))return result;

  const std::string opf=container.fullPath;
  if(!zip.getInflatedFileSize(opf.c_str(),&bytes) ||
     !bytes || bytes>1024u*1024u)return result;
  const auto slash=opf.find_last_of('/');
  const std::string base=slash==std::string::npos?"":opf.substr(0,slash+1);
  // cache=nullptr means the original CrossPoint OPF parser collects metadata
  // and EPUB2/EPUB3 cover references without creating spine/TOC cache files.
  ContentOpfParser parser("",base,bytes,nullptr,false);
  if(!parser.setup() || !zip.readFileToStream(opf.c_str(),parser,1024))
    return result;
  if(!metadataReady && (!parser.title.empty()||!parser.author.empty())) {
    result.metadata=saveShelfDetails(bookPath,{parser.title,parser.author});
    result.updated=result.metadata;
  }

  // An EPUB with no declared image cover is still a valid book.
  // Do not synthesize a cover from the first XHTML page.
  const std::string& cover=parser.coverItemHref;
  if((coverReady && flowReady) || !isSafeEpubItem(cover))return result;
  bytes=0;
  if(!zip.getInflatedFileSize(cover.c_str(),&bytes) ||
     bytes<8 || bytes>2u*1024u*1024u)return result;
  std::unique_ptr<uint8_t,decltype(&std::free)> image(
      zip.readFileToMemory(cover.c_str(),&bytes,false),&std::free);
  if(!image || bytes<8 || bytes>2u*1024u*1024u)return result;
  PngPreview decoded;
  bool decodedOk=decodePngPreview(image.get(),bytes,decoded,!flowReady);
  if(!decodedOk && image.get()[0]==0xff && image.get()[1]==0xd8) {
    if(!flowReady && decodeJpegRgbPreview(image.get(),bytes,decoded)) {
      if(!coverReady) {
        decoded.gray.resize(size_t(decoded.width)*decoded.height);
        for(size_t i=0;i<decoded.gray.size();++i)
          decoded.gray[i]=uint8_t((77u*decoded.rgb[3*i]+150u*decoded.rgb[3*i+1]+29u*decoded.rgb[3*i+2]+128u)>>8);
      }
      decodedOk=true;
    } else decodedOk=decodeJpegPreview(image.get(),bytes,decoded);
  }
  if(!decodedOk)return result;
  ShelfCover thumb{};
  if(!coverReady && makeShelfCover(decoded,thumb)) {
    result.cover=saveShelfCover(bookPath,thumb);
    result.updated=result.updated || result.cover;
  }
  if(!flowReady && !decoded.rgb.empty()) {
    FlowCover flow;
    if(makeFlowCover(decoded.rgb.data(),decoded.rgb.size(),decoded.width,decoded.height,flow)) {
      result.flowCover=saveFlowCover(bookPath,flow);
      result.updated=result.updated || result.flowCover;
    }
  }
  return result;
}
} // namespace books
