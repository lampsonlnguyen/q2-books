// SPDX-License-Identifier: MIT
#pragma once
// OPDS 1.x Atom SAX parser and safe same-origin EPUB downloads for Q2/Linux.
// Portable XML/ZIP reader infrastructure is reused; no ESP32 networking.
#include "ConnectedHttp.hpp"
#include "ShelfDetails.hpp"
#include <expat.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace crosspod {
struct OpdsEntry {
  enum class Kind {Catalog, Epub};
  Kind kind=Kind::Catalog;
  std::string title;
  std::string author;
  std::string id;
  std::string href;
};
struct OpdsFeed {
  std::string title;
  std::vector<OpdsEntry> entries;
  std::string error;
};
inline std::string opdsLocalName(const XML_Char* raw) {
  if(!raw)return {};
  const std::string_view name(raw);
  const auto pos=name.find_last_of("|:");
  return std::string(name.substr(pos==std::string_view::npos?0:pos+1));
}
struct OpdsParserState {
  OpdsFeed* feed=nullptr;
  OpdsEntry current{};
  std::string text;
  std::string field;
  std::string pageUrl;
  bool inEntry=false;
  bool inAuthor=false;
  bool entryLink=false;
  bool valid=true;
  size_t depth=0;
  size_t bookCount=0;
};
inline const char* opdsAttribute(const XML_Char** attrs,const char* name) {
  for(size_t i=0;attrs && attrs[i] && attrs[i+1];i+=2)
    if(opdsLocalName(attrs[i])==name)return attrs[i+1];
  return nullptr;
}
inline void XMLCALL opdsStart(void* opaque,const XML_Char* name,
                             const XML_Char** attrs) {
  auto& state=*static_cast<OpdsParserState*>(opaque);
  if(++state.depth>40){state.valid=false;return;}
  const std::string tag=opdsLocalName(name);
  if(tag=="entry") {
    state.inEntry=true;state.inAuthor=false;state.entryLink=false;
    state.current={};
  } else if(tag=="author" && state.inEntry)state.inAuthor=true;
  else if(tag=="link" && state.inEntry) {
    const char* href=opdsAttribute(attrs,"href");
    const char* type=opdsAttribute(attrs,"type");
    const char* rel=opdsAttribute(attrs,"rel");
    if(href && std::strlen(href)<1500) {
      const std::string resolved=resolveHttpHref(state.pageUrl,href);
      if(!resolved.empty()) {
        const std::string mime=type?type:"";
        const std::string relationship=rel?rel:"";
        const bool epub=(mime.find("application/epub+zip")!=std::string::npos);
        const bool navigation=mime.find("application/atom+xml")!=std::string::npos ||
          relationship.find("subsection")!=std::string::npos ||
          relationship.find("navigation")!=std::string::npos;
        if(epub) {
          state.current.kind=OpdsEntry::Kind::Epub;
          state.current.href=resolved;
          state.entryLink=true;
        } else if(!state.entryLink && navigation) {
          state.current.kind=OpdsEntry::Kind::Catalog;
          state.current.href=resolved;
        }
      }
    }
  }
  if(tag=="title"||tag=="name"||tag=="id") {
    state.field=tag;state.text.clear();
  }
}
inline void XMLCALL opdsCharacter(void* opaque,const XML_Char* data,int size) {
  auto& state=*static_cast<OpdsParserState*>(opaque);
  if(!state.field.empty() && size>0 && state.text.size()<1024) {
    const size_t take=std::min<size_t>(
        static_cast<size_t>(size),1024-state.text.size());
    state.text.append(data,take);
  }
}
inline void XMLCALL opdsEnd(void* opaque,const XML_Char* name) {
  auto& state=*static_cast<OpdsParserState*>(opaque);
  const std::string tag=opdsLocalName(name);
  if(tag==state.field) {
    if(state.inEntry) {
      if(tag=="title" && !state.inAuthor)state.current.title=state.text;
      else if(tag=="name"&&state.inAuthor)state.current.author=state.text;
      else if(tag=="id")state.current.id=state.text;
    } else if(tag=="title"&&state.feed->title.empty())
      state.feed->title=state.text;
    state.field.clear();state.text.clear();
  }
  if(tag=="author")state.inAuthor=false;
  if(tag=="entry") {
    if(!state.current.href.empty() && state.feed->entries.size()<96) {
      if(state.current.title.empty())state.current.title="Untitled";
      state.feed->entries.push_back(std::move(state.current));
    }
    state.inEntry=false;state.entryLink=false;
  }
  if(state.depth) --state.depth;
}
inline OpdsFeed parseOpdsFeed(const std::string& pageUrl,
                              const std::string& xml) {
  OpdsFeed feed;
  if(!parseHttpUrl(pageUrl).valid || xml.empty() ||
     xml.size()>512u*1024u) {
    feed.error="Invalid catalog URL or XML size";return feed;
  }
  OpdsParserState state;
  state.feed=&feed;state.pageUrl=pageUrl;
  XML_Parser parser=XML_ParserCreateNS(nullptr,'|');
  if(!parser){feed.error="XML parser unavailable";return feed;}
  XML_SetUserData(parser,&state);
  XML_SetElementHandler(parser,opdsStart,opdsEnd);
  XML_SetCharacterDataHandler(parser,opdsCharacter);
  const bool valid=XML_Parse(parser,xml.data(),
      static_cast<int>(xml.size()),XML_TRUE)==XML_STATUS_OK && state.valid;
  XML_ParserFree(parser);
  if(!valid){feed.entries.clear();feed.error="Invalid OPDS XML";}
  return feed;
}
inline OpdsFeed fetchOpdsFeed(const std::string& url) {
  OpdsFeed feed;
  std::string xml;
  const auto response=httpGetText(url,xml);
  if(!response.ok()) {
    feed.error=response.error.empty()?"Catalog request failed":response.error;
    return feed;
  }
  return parseOpdsFeed(url,xml);
}
inline uint64_t opdsFnv64(const std::string& text) {
  uint64_t hash=14695981039346656037ull;
  for(unsigned char byte:text){hash^=byte;hash*=1099511628211ull;}
  return hash;
}
inline std::string opdsDestination(const std::string& booksRoot,
                                   const OpdsEntry& entry) {
  if(entry.kind!=OpdsEntry::Kind::Epub ||
     booksRoot.empty() || booksRoot.size()>750 ||
     !parseHttpUrl(entry.href).valid)return {};
  std::string label;
  for(unsigned char c:entry.title) {
    if(label.size()>=32)break;
    if(std::isalnum(c) && c<128)label+=static_cast<char>(c);
    else if((c==' '||c=='-'||c=='_') && !label.empty()&&label.back()!='-')
      label+='-';
  }
  if(label.empty())label="Book";
  char id[24]{};
  std::snprintf(id,sizeof(id),"-%016llx.epub",
                static_cast<unsigned long long>(opdsFnv64(entry.href)));
  return booksRoot+"/Online/"+label+id;
}
inline bool opdsDownloadEpub(const std::string& booksRoot,
                             const OpdsEntry& entry,
                             std::string& saved,std::string& error) {
  saved.clear();error.clear();
  const std::string file=opdsDestination(booksRoot,entry);
  if(file.empty()) {error="Invalid download link";return false;}
  const auto root=parseHttpUrl(entry.href);
  if(!root.valid) {error="HTTPS requires a supported TLS transport";return false;}
  const std::string folder=booksRoot+"/Online";
  struct stat info{};
  if(::lstat(booksRoot.c_str(),&info)!=0 || !S_ISDIR(info.st_mode) ||
     (::lstat(folder.c_str(),&info)==0 && !S_ISDIR(info.st_mode))) {
    error="Books directory unavailable";return false;
  }
  if(::mkdir(folder.c_str(),0755)!=0 && errno!=EEXIST) {
    error="Cannot create Online folder";return false;
  }
  if(::lstat(file.c_str(),&info)==0) {
    if(S_ISREG(info.st_mode)) {saved=file;return true;}
    error="File destination not regular";return false;
  }
  const std::string temp=file+".part";
  int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW,0600);
  if(fd<0) {error="Download already in progress or unwritable";return false;}
  size_t bytes=0;
  bool writeOk=true;
  char first[4]{};size_t firstCount=0;
  const auto response=httpRequest(entry.href,[&](const char* block,size_t len) {
    const size_t check=std::min(len,sizeof(first)-firstCount);
    if(check){std::memcpy(first+firstCount,block,check);firstCount+=check;}
    for(size_t off=0;off<len;) {
      const ssize_t n=::write(fd,block+off,len-off);
      if(n<=0){writeOk=false;return false;}
      off+=static_cast<size_t>(n);
    }
    bytes+=len;return true;
  },32u*1024u*1024u);
  if(::fsync(fd)!=0)writeOk=false;
  if(::close(fd)!=0)writeOk=false;
  if(!response.ok() || !writeOk || bytes<32 ||
     firstCount!=4 || first[0]!='P'||first[1]!='K' ||
     first[2]!='\003'||first[3]!='\004') {
    ::unlink(temp.c_str());
    error=response.error.empty()?"Not a complete EPUB ZIP":response.error;
    return false;
  }
  // Never follow an existing file or overwrite a different edition.
  if(::lstat(file.c_str(),&info)==0) {
    ::unlink(temp.c_str());error="Destination already exists";return false;
  }
  if(::rename(temp.c_str(),file.c_str())!=0) {
    ::unlink(temp.c_str());error="Cannot save EPUB";return false;
  }
  (void)saveShelfDetails(file,{entry.title,entry.author});
  saved=file;
  return true;
}
} // namespace crosspod
