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
#include <dirent.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace crosspod {
struct OpdsEntry {
  // Search and More are rows the shelf adds for a feed's search and
  // rel="next" links; they are not entries of the feed.
  enum class Kind {Catalog, Epub, Search, More};
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
  std::string nextHref;        // rel="next": the following page of this feed
  std::string searchTemplate;  // an Atom URL template with {searchTerms}
  std::string searchDescription;  // an OpenSearch description to fetch
};
inline std::string opdsLocalName(const XML_Char* raw) {
  if(!raw)return {};
  const std::string_view name(raw);
  const auto pos=name.find_last_of("|:");
  return std::string(name.substr(pos==std::string_view::npos?0:pos+1));
}
inline const char* opdsAttribute(const XML_Char** attrs,const char* name) {
  for(size_t i=0;attrs && attrs[i] && attrs[i+1];i+=2)
    if(opdsLocalName(attrs[i])==name)return attrs[i+1];
  return nullptr;
}
// A URL template ("{searchTerms}", OpenSearch's optional "{name?}") made
// absolute against the page: resolve with a placeholder that survives
// URL resolution, then put the braces back.
inline std::string resolveOpdsTemplate(const std::string& pageUrl,std::string href) {
  const std::string mark="CROSSPODSEARCHTERMS";
  const size_t at=href.find("{searchTerms}");
  if(at==std::string::npos)return {};
  href.replace(at,13,mark);
  std::string resolved=resolveHttpHref(pageUrl,href);
  const size_t back=resolved.find(mark);
  if(back==std::string::npos)return {};
  resolved.replace(back,mark.size(),"{searchTerms}");
  return resolved;
}
inline std::string opdsPercentEncode(const std::string& text) {
  static const char* hex="0123456789ABCDEF";
  std::string out;
  for(unsigned char c:text) {
    if((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c=='.'||c=='~')
      out.push_back(static_cast<char>(c));
    else {out.push_back('%');out.push_back(hex[c>>4]);out.push_back(hex[c&15]);}
  }
  return out;
}
// Fill a search template: {searchTerms} gets the encoded query; other
// OpenSearch parameters get their usual first-page values or nothing.
inline std::string fillOpdsSearchTemplate(const std::string& tpl,const std::string& query) {
  if(tpl.find("{searchTerms}")==std::string::npos || query.empty() || query.size()>200)return {};
  std::string out;
  for(size_t i=0;i<tpl.size();) {
    if(tpl[i]!='{'){out.push_back(tpl[i++]);continue;}
    const size_t close=tpl.find('}',i);
    if(close==std::string::npos || close-i>40)return {};
    std::string name=tpl.substr(i+1,close-i-1);
    if(!name.empty()&&name.back()=='?')name.pop_back();
    if(const size_t colon=name.find(':');colon!=std::string::npos)name.erase(0,colon+1);
    if(name=="searchTerms")out+=opdsPercentEncode(query);
    else if(name=="startPage"||name=="startIndex")out+= name=="startPage"?"1":"0";
    else if(name=="count")out+="20";
    else if(name=="language"||name=="inputEncoding"||name=="outputEncoding")
      out+= name=="language"?"*":"UTF-8";
    i=close+1;
  }
  return parseHttpUrl(out).valid?out:std::string();
}
// The Atom template from an OpenSearch description document.
inline std::string parseOpenSearchTemplate(const std::string& pageUrl,const std::string& xml) {
  if(xml.size()>64u*1024u)return {};
  struct State {std::string found;std::string page;int depth=0;bool bad=false;} state;
  state.page=pageUrl;
  XML_Parser parser=XML_ParserCreateNS(nullptr,'|');
  if(!parser)return {};
  XML_SetUserData(parser,&state);
  XML_SetElementHandler(parser,
    [](void* opaque,const XML_Char* name,const XML_Char** attrs) {
      auto& st=*static_cast<State*>(opaque);
      if(++st.depth>16){st.bad=true;return;}
      if(opdsLocalName(name)!="Url" || !st.found.empty())return;
      const char* type=opdsAttribute(attrs,"type");
      const char* tpl=opdsAttribute(attrs,"template");
      if(!type||!tpl||std::strlen(tpl)>1500)return;
      if(std::string(type).find("atom+xml")==std::string::npos)return;
      st.found=resolveOpdsTemplate(st.page,tpl);
    },
    [](void* opaque,const XML_Char*) {auto& st=*static_cast<State*>(opaque);if(st.depth)--st.depth;});
  const bool ok=XML_Parse(parser,xml.data(),static_cast<int>(xml.size()),XML_TRUE)==XML_STATUS_OK;
  XML_ParserFree(parser);
  return ok&&!state.bad?state.found:std::string();
}
// The URL to search a feed for |query|, fetching its OpenSearch description
// when the feed links one. Empty, with |error| set, when there is none.
inline std::string opdsSearchUrl(const OpdsFeed& feed,const std::string& query,std::string& error) {
  std::string tpl=feed.searchTemplate;
  if(tpl.empty() && !feed.searchDescription.empty()) {
    std::string xml;
    const auto response=httpGetText(feed.searchDescription,xml);
    if(!response.ok()){error=response.error.empty()?"Search description failed":response.error;return {};}
    tpl=parseOpenSearchTemplate(feed.searchDescription,xml);
  }
  if(tpl.empty()){error="This catalog has no search";return {};}
  const std::string url=fillOpdsSearchTemplate(tpl,query);
  if(url.empty())error="Bad search template";
  return url;
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
inline void XMLCALL opdsStart(void* opaque,const XML_Char* name,
                             const XML_Char** attrs) {
  auto& state=*static_cast<OpdsParserState*>(opaque);
  if(++state.depth>40){state.valid=false;return;}
  const std::string tag=opdsLocalName(name);
  if(tag=="entry") {
    state.inEntry=true;state.inAuthor=false;state.entryLink=false;
    state.current={};
  } else if(tag=="author" && state.inEntry)state.inAuthor=true;
  else if(tag=="link" && !state.inEntry && state.depth==2) {
    // Feed-level links (children of <feed>): paging and search.
    const char* href=opdsAttribute(attrs,"href");
    const char* type=opdsAttribute(attrs,"type");
    const char* rel=opdsAttribute(attrs,"rel");
    const std::string relationship=rel?rel:"",mime=type?type:"";
    if(href && std::strlen(href)<1500) {
      if(relationship=="next") {
        state.feed->nextHref=resolveHttpHref(state.pageUrl,href);
      } else if(relationship=="search") {
        const std::string raw=href;
        if(mime.find("opensearchdescription")!=std::string::npos)
          state.feed->searchDescription=resolveHttpHref(state.pageUrl,raw);
        else if(raw.find("{searchTerms}")!=std::string::npos &&
                (mime.empty()||mime.find("atom+xml")!=std::string::npos))
          state.feed->searchTemplate=resolveOpdsTemplate(state.pageUrl,raw);
      }
    }
  } else if(tag=="link" && state.inEntry) {
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
// Removes .part files in Books/Online that no running download holds (a
// reader killed mid-transfer leaves one). Returns how many were removed.
inline int sweepStaleOpdsDownloads(const std::string& booksRoot) {
  const std::string folder=booksRoot+"/Online";
  DIR* dir=::opendir(folder.c_str());
  if(!dir)return 0;
  int removed=0;
  while(const dirent* entry=::readdir(dir)) {
    const std::string name=entry->d_name;
    if(name.size()<=5 || name.compare(name.size()-5,5,".part")!=0)continue;
    const std::string path=folder+"/"+name;
    const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_CLOEXEC);
    if(fd<0)continue;
    struct stat info{};
    if(::fstat(fd,&info)==0 && S_ISREG(info.st_mode) && ::flock(fd,LOCK_EX|LOCK_NB)==0 &&
       ::unlink(path.c_str())==0)++removed;
    ::close(fd);
  }
  ::closedir(dir);
  return removed;
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
  // The .part is locked for as long as its download runs. One left by a reader
  // that was killed mid-transfer (Q2 Pod SIGKILLs 3 s after Return's SIGTERM)
  // holds no lock: take it over, instead of refusing every later attempt.
  int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0) {error="Download destination unwritable";return false;}
  struct stat partInfo{};
  if(::fstat(fd,&partInfo)!=0 || !S_ISREG(partInfo.st_mode)) {
    ::close(fd);error="File destination not regular";return false;
  }
  if(::flock(fd,LOCK_EX|LOCK_NB)!=0) {
    ::close(fd);error="Download already in progress";return false;
  }
  if(::ftruncate(fd,0)!=0) {::close(fd);::unlink(temp.c_str());error="Cannot save EPUB";return false;}
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
  // Keep the .part locked until it is renamed or removed, so no other
  // download can take it over in between.
  struct PartGuard {int fd;~PartGuard(){::close(fd);}} part{fd};
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
