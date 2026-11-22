// SPDX-License-Identifier: MIT
// POSIX Epub runtime adapter for the ORIGINAL CrossPoint chapter/Section engine.
// All parsing, metadata caching, CSS and ZIP IO use unmodified upstream code.
// Only file paths and the original FreeInk firmware lifecycle are adapted.
#include "Epub/Epub.h"
#include <ContainerParser.h>
#include <ContentOpfParser.h>
#include <TocNavParser.h>
#include <TocNcxParser.h>
#include <ZipFile.h>
#include <HalStorage.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <sys/stat.h>

Epub::Epub(std::string path,const std::string& cacheDir)
  : filepath(std::move(path)),archive(std::make_unique<ZipFile>(filepath)),cachePath(cacheDir),
    bookMetadataCache(std::make_unique<BookMetadataCache>(cacheDir)),
    cssParser(std::make_unique<CssParser>(cacheDir)) {}

Epub::~Epub() = default;

const std::string& Epub::getCachePath() const {return cachePath;}
const std::string& Epub::getPath() const {return filepath;}
const std::string& Epub::getTitle() const {return bookMetadataCache->coreMetadata.title;}
const std::string& Epub::getAuthor() const {return bookMetadataCache->coreMetadata.author;}
const std::string& Epub::getLanguage() const {return bookMetadataCache->coreMetadata.language;}

void Epub::setupCacheDir() const {
  Storage.mkdir(cachePath.c_str());
  Storage.mkdir((cachePath+"/html").c_str());
  Storage.mkdir((cachePath+"/sections").c_str());
}

bool Epub::load(bool buildIfMissing,bool skipLoadingCss) {
  setupCacheDir();
  if(!buildIfMissing)return bookMetadataCache->load();
  auto& zip=*archive;
  if(!zip.loadAllFileStatSlims())return false;
  // Protected books require an implementation of FreeInk's protected read
  // path. Never silently render encrypted EPUB data as if it were plain.
  size_t encrypted=0;
  if(zip.getInflatedFileSize("META-INF/encryption.xml",&encrypted) && encrypted)
    return false;
  size_t n=0;
  if(!zip.getInflatedFileSize("META-INF/container.xml",&n)||n==0||n>4u*1024u*1024u)
    return false;
  ContainerParser container(n);
  if(!container.setup() ||
     !zip.readFileToStream("META-INF/container.xml",container,512) ||
     container.fullPath.empty()||container.fullPath[0]=='/' ||
     container.fullPath.find("..")!=std::string::npos)return false;
  const std::string opf=container.fullPath;
  n=0;
  if(!zip.getInflatedFileSize(opf.c_str(),&n)||n==0||n>4u*1024u*1024u)
    return false;
  const size_t slash=opf.find_last_of('/');
  contentBasePath=slash==std::string::npos?"":opf.substr(0,slash+1);
  auto& cache=*bookMetadataCache;
  if(!cache.beginWrite()||!cache.beginContentOpfPass())return false;
  BookMetadataCache::BookMetadata info;
  std::string tocNavPath,tocNcxPath;
  {
    ContentOpfParser parser(cachePath,contentBasePath,n,&cache,false);
    if(!parser.setup() || !zip.readFileToStream(opf.c_str(),parser,1024))
      return false;
    info.title=parser.title;
    info.author=parser.author;
    info.language=parser.language;
    info.coverItemHref=parser.coverItemHref;
    info.textReferenceHref=parser.textReferenceHref;
    tocNavPath=parser.tocNavPath;
    tocNcxPath=parser.tocNcxPath;
  }
  if(!cache.endContentOpfPass() || !cache.beginTocPass())return false;
  // Use the original CrossPoint EPUB3 nav.xhtml and EPUB2 toc.ncx parsers.
  // Entries preserve chapter titles, anchors and nested TOC depth.
  const auto parseToc=[&](const std::string& relative,bool isNav) {
    if(relative.empty() || relative.size()>512 || relative.front()=='/' ||
       relative.find("..")!=std::string::npos ||
       relative.find('\\')!=std::string::npos)return false;
    const std::string archivePath=
       relative.rfind(contentBasePath,0)==0?relative:contentBasePath+relative;
    size_t bytes=0;
    if(!zip.getInflatedFileSize(archivePath.c_str(),&bytes) ||
       !bytes || bytes>1024u*1024u)return false;
    const size_t slash=archivePath.find_last_of('/');
    const std::string navBase=
       slash==std::string::npos?"":archivePath.substr(0,slash+1);
    if(isNav) {
      TocNavParser parser(navBase,bytes,&cache);
      return parser.setup()&&zip.readFileToStream(archivePath.c_str(),parser,1024);
    }
    TocNcxParser parser(navBase,bytes,&cache);
    return parser.setup()&&zip.readFileToStream(archivePath.c_str(),parser,1024);
  };
  const bool tocLoaded=parseToc(tocNavPath,true)||
                       parseToc(tocNcxPath,false);
  std::printf("CrossPod original TOC: %s\n",
              tocLoaded?"EPUB nav/NCX parsed":"spine-only fallback");
  if(!cache.endTocPass() || !cache.endWrite() ||
     !cache.buildBookBin(filepath,info) || !cache.load() ||
     cache.getSpineCount()==0)return false;
  if(!skipLoadingCss && cssParser) {
    size_t count=0;
    zip.enumerateFilePaths([&](std::string_view name){
      if(count>=32||name.size()<4||name.substr(name.size()-4)!=".css")return;
      ++count;
      size_t bytes=0;
      const std::string path(name);
      if(!zip.getInflatedFileSize(path.c_str(),&bytes)||
         bytes==0||bytes>256*1024)return;
      std::unique_ptr<uint8_t,decltype(&std::free)> data(
        zip.readFileToMemory(path.c_str(),&bytes,false),&std::free);
      if(!data)return;
      HalFile temp(std::tmpfile());
      if(temp && temp.write(data.get(),bytes)==bytes && temp.seek(0))
        (void)cssParser->loadFromStream(temp);
    });
    (void)cssParser->saveToCache(true);
  }
  return true;
}

uint8_t* Epub::readItemContentsToBytes(const std::string& itemHref,size_t* size,
                                       bool trailingNullByte) const {
  if(itemHref.empty()||itemHref.front()=='/' ||
     itemHref.find("..")!=std::string::npos || itemHref.find('\\')!=std::string::npos)
    return nullptr;
  return archive->readFileToMemory(itemHref.c_str(),size,trailingNullByte);
}

bool Epub::readItemContentsToStream(const std::string& itemHref,Print& output,
                                    size_t chunkSize,bool earlyStop) const {
  if(itemHref.empty()||itemHref.front()=='/' ||
     itemHref.find("..")!=std::string::npos || itemHref.find('\\')!=std::string::npos)
    return false;
  auto& zip=*archive;
  if(zip.readFileToStream(itemHref.c_str(),output,chunkSize,earlyStop))return true;
  if(!contentBasePath.empty() && itemHref.rfind(contentBasePath,0)!=0)
    return zip.readFileToStream((contentBasePath+itemHref).c_str(),output,chunkSize,earlyStop);
  return false;
}

BookMetadataCache::SpineEntry Epub::getSpineItem(int n) const {
  if(n<0 || n>=bookMetadataCache->getSpineCount())return {};
  return bookMetadataCache->getSpineEntry(n);
}
BookMetadataCache::TocEntry Epub::getTocItem(int n) const {
  if(n<0 || n>=bookMetadataCache->getTocCount())return {};
  return bookMetadataCache->getTocEntry(n);
}
int Epub::getSpineItemsCount() const {return bookMetadataCache->getSpineCount();}
int Epub::getTocItemsCount() const {return bookMetadataCache->getTocCount();}
int Epub::getTocIndexForSpineIndex(int n) const {
  return getSpineItem(n).tocIndex;
}
bool Epub::getItemSize(const std::string& itemHref,size_t* size) const {
  if(!size || itemHref.empty())return false;
  return archive->getInflatedFileSize(itemHref.c_str(),size);
}
// The upstream CrossPoint size-progress helpers were unnecessary before
// KOSync and previously stubbed out. Use the exact original metadata LUT,
// not a rough chapter index / page-count approximation.
size_t Epub::getCumulativeSpineItemSize(int index) const {
  if(!bookMetadataCache || !bookMetadataCache->isLoaded() ||
     index<0 || index>=bookMetadataCache->getSpineCount())return 0;
  return bookMetadataCache->getCumulativeSize(index);
}
int Epub::getSpineIndexForSize(size_t bytes) const {
  if(!bookMetadataCache || !bookMetadataCache->isLoaded() ||
     bytes>UINT32_MAX)return -1;
  return bookMetadataCache->getSpineIndexForSize(
      static_cast<uint32_t>(bytes));
}
size_t Epub::getBookSize() const {
  if(!bookMetadataCache || !bookMetadataCache->isLoaded() ||
     bookMetadataCache->getSpineCount()==0)return 0;
  return getCumulativeSpineItemSize(getSpineItemsCount()-1);
}
float Epub::calculateProgress(int spineIndex,float intraSpine) const {
  const auto total=getBookSize();
  if(!total || spineIndex<0||spineIndex>=getSpineItemsCount())return 0.0f;
  const auto prior=spineIndex>=1?
      getCumulativeSpineItemSize(spineIndex-1):0;
  const auto through=getCumulativeSpineItemSize(spineIndex);
  const auto current=through>=prior?through-prior:0;
  return std::clamp((static_cast<float>(prior)+
      std::clamp(intraSpine,0.0f,1.0f)*static_cast<float>(current))/
      static_cast<float>(total),0.0f,1.0f);
}
