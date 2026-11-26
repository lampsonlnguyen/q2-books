// SPDX-License-Identifier: MIT
#pragma once
// Scan one never-opened EPUB's OPF and cover on demand, without allocating
// a CrossPoint Section, paginating a chapter, or changing the reader engine.
#include "ShelfBookInfo.hpp"
#include "ShelfCover.hpp"
#include "CoverImage.hpp"
#include "MobiPackage.hpp"
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
inline bool readEpubCover(ZipFile& zip,const std::string& cover,PngPreview& decoded) {
  size_t size=0;
  constexpr size_t maxCoverBytes=4u*1024u*1024u;
  if(!isSafeEpubItem(cover) || !zip.getInflatedFileSize(cover.c_str(),&size) ||
     size<8 || size>maxCoverBytes)return false;
  std::unique_ptr<uint8_t,decltype(&std::free)> bytes(
      zip.readFileToMemory(cover.c_str(),&size,false),&std::free);
  return bytes && size<=maxCoverBytes && decodeCoverImage(bytes.get(),size,decoded);
}
inline ShelfIndexResult indexShelfBook(const std::string& bookPath,ShelfBookInfo* requested=nullptr,
                                      bool indexArtwork=true) {
  ShelfIndexResult result{};
  if(bookPath.empty() || bookPath.size()>1000)return result;
  if(!isEpubFilename(bookPath) && !isMobiFilename(bookPath))return result;  // source documents have no EPUB cover
  struct stat st{};
  if(::lstat(bookPath.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size<=0 || st.st_size>128ll*1024*1024)return result;

  ShelfBookInfo info;
  const bool infoReady=loadShelfBookInfo(bookPath,info);
  if(requested && infoReady)*requested=info;
  ShelfDetails existing{};
  const bool metadataReady=loadShelfDetails(bookPath,existing);
  ShelfCover cachedCover{};
  const bool coverReady=indexArtwork && loadShelfCover(bookPath,cachedCover);
  FlowCover cachedFlow;
  const bool flowReady=indexArtwork && loadFlowCover(bookPath,cachedFlow);
  result.flowCover=flowReady;
  result.metadata=metadataReady;
  result.cover=coverReady;
  if(infoReady && metadataReady && (!indexArtwork || info.cover.empty() ||
      (coverReady && flowReady)))return result;

  if(isMobiFilename(bookPath)) {
    mobipkg::Book book;
    std::string error;
    if(!mobipkg::readBook(bookPath,book,error,true,indexArtwork && (!coverReady || !flowReady)))return result;
    if(!infoReady) {
      info={book.title,book.author,book.publisher,book.language,book.description,
            {},book.isbn,book.asin,{},book.hasCover?"mobi:cover":"",{}};
      if(requested)*requested=info;
      result.updated=saveShelfBookInfo(bookPath,info);
    }
    if(!metadataReady) {
      result.metadata=saveShelfDetails(bookPath,{book.title,book.author});
      result.updated=result.updated || result.metadata;
    }
    PngPreview decoded;
    if(!indexArtwork || book.cover.empty() ||
       !decodeCoverImage(reinterpret_cast<const uint8_t*>(book.cover.data()),book.cover.size(),decoded))return result;
    if(!coverReady && decoded.gray.empty()) {
      decoded.gray.resize(size_t(decoded.width)*decoded.height);
      for(size_t i=0;i<decoded.gray.size();++i)
        decoded.gray[i]=uint8_t((77u*decoded.rgb[3*i]+150u*decoded.rgb[3*i+1]+29u*decoded.rgb[3*i+2]+128u)>>8);
    }
    ShelfCover thumb{};
    if(!coverReady && makeShelfCover(decoded,thumb)) {
      result.cover=saveShelfCover(bookPath,thumb);
      result.updated=result.updated || result.cover;
    }
    FlowCover flow;
    if(!flowReady && !decoded.rgb.empty() && makeColorCover(decoded,flow)) {
      result.flowCover=saveFlowCover(bookPath,flow);
      result.updated=result.updated || result.flowCover;
    }
    return result;
  }

  // Keep only ZIP metadata and at most one bounded image in memory.
  // ZipFile retains a const reference to bookPath for its entire lifetime.
  ZipFile zip(bookPath);
  // Metadata lookup needs only two named entries; avoid an archive-wide index.
  if(indexArtwork && !zip.loadAllFileStatSlims())return result;
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
  if(!infoReady) {
    info={parser.title,parser.author,parser.publisher,parser.language,parser.description,
          parser.identifier,parser.isbn,parser.asin,parser.series,parser.coverItemHref,parser.seriesIndex};
    info.series=normalizeSeriesName(info.series);
    if(info.series.empty())info.volume.reset();
    if(requested)*requested=info;
    result.updated=saveShelfBookInfo(bookPath,info);
  }
  if(!metadataReady) {
    result.metadata=saveShelfDetails(bookPath,{parser.title,parser.author});
    result.updated=result.updated || result.metadata;
  }

  // An EPUB with no declared image cover is still a valid book.
  // Do not synthesize a cover from the first XHTML page.
  const std::string& cover=parser.coverItemHref;
  if(!indexArtwork || (coverReady && flowReady) || !isSafeEpubItem(cover))return result;
  PngPreview decoded;
  if(!readEpubCover(zip,cover,decoded))return result;
  if(!coverReady && decoded.gray.empty()) {
    decoded.gray.resize(size_t(decoded.width)*decoded.height);
    for(size_t i=0;i<decoded.gray.size();++i)
      decoded.gray[i]=uint8_t((77u*decoded.rgb[3*i]+150u*decoded.rgb[3*i+1]+29u*decoded.rgb[3*i+2]+128u)>>8);
  }
  ShelfCover thumb{};
  if(!coverReady && makeShelfCover(decoded,thumb)) {
    result.cover=saveShelfCover(bookPath,thumb);
    result.updated=result.updated || result.cover;
  }
  if(!flowReady && !decoded.rgb.empty()) {
    FlowCover flow;
    if(makeColorCover(decoded,flow)) {
      result.flowCover=saveFlowCover(bookPath,flow);
      result.updated=result.updated || result.flowCover;
    }
  }
  return result;
}
} // namespace books
