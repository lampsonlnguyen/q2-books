#pragma once
// RD-04 plain-text reading. A .txt book is wrapped, once, as a small EPUB in
// the cache (a stored ZIP: mimetype, container, OPF, NCX and the text split
// into ~32 KiB XHTML parts made by upstream CrossPoint's TxtToHtml). The
// reader then opens that package with the same engine as any EPUB, so
// positions, bookmarks, highlights, percent and focus reading all work, while
// the book's identity (positions, bookmarks, sync) stays the .txt path.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

#include "ShelfLibrary.hpp"
#include "TxtToHtml.h"

namespace books {

inline constexpr size_t kTxtMaxBytes=32u*1024u*1024u;  // larger TXT is refused
inline constexpr size_t kTxtPartBytes=32u*1024u;       // source bytes per XHTML part
inline constexpr int kTxtPackageVersion=1;

namespace txtpkg {
inline uint32_t crc32(const std::string& data) {
  static uint32_t table[256];
  static bool ready=false;
  if(!ready) {
    for(uint32_t i=0;i<256;++i) {
      uint32_t c=i;
      for(int k=0;k<8;++k)c=c&1u?0xEDB88320u^(c>>1):c>>1;
      table[i]=c;
    }
    ready=true;
  }
  uint32_t c=0xFFFFFFFFu;
  for(unsigned char b:data)c=table[(c^b)&255u]^(c>>8);
  return c^0xFFFFFFFFu;
}
inline bool validUtf8(std::string_view s) {
  for(size_t i=0;i<s.size();) {
    const unsigned char b=static_cast<unsigned char>(s[i]);
    size_t n=b<0x80?1:(b>>5)==6?2:(b>>4)==14?3:(b>>3)==30?4:0;
    if(!n || i+n>s.size())return false;
    for(size_t k=1;k<n;++k)
      if((static_cast<unsigned char>(s[i+k])&0xC0)!=0x80)return false;
    i+=n;
  }
  return true;
}
// Not UTF-8: read it as Windows-1252/Latin-1, the usual legacy TXT encoding.
inline std::string latin1ToUtf8(std::string_view s) {
  std::string out;
  out.reserve(s.size()+s.size()/8);
  for(unsigned char b:s) {
    if(b<0x80)out.push_back(static_cast<char>(b));
    else {out.push_back(static_cast<char>(0xC0|(b>>6)));out.push_back(static_cast<char>(0x80|(b&0x3F)));}
  }
  return out;
}
// Split points: a part ends after a blank line, else a newline, else a UTF-8
// character boundary, near kTxtPartBytes.
inline std::vector<std::string_view> splitParts(std::string_view text) {
  std::vector<std::string_view> parts;
  while(!text.empty()) {
    if(text.size()<=kTxtPartBytes+kTxtPartBytes/4){parts.push_back(text);break;}
    const std::string_view window=text.substr(0,kTxtPartBytes);
    size_t cut=window.rfind("\n\n");
    if(cut==std::string_view::npos || cut<kTxtPartBytes/2)cut=window.rfind('\n');
    if(cut==std::string_view::npos || cut<kTxtPartBytes/2) {
      cut=kTxtPartBytes;
      while(cut>0 && (static_cast<unsigned char>(text[cut])&0xC0)==0x80)--cut;
    } else ++cut;  // keep the newline with the part before it
    if(cut==0)cut=kTxtPartBytes;
    parts.push_back(text.substr(0,cut));
    text.remove_prefix(cut);
  }
  if(parts.empty())parts.push_back(std::string_view());
  return parts;
}
struct StringPrint final : Print {
  std::string out;
  size_t write(uint8_t byte) override {out.push_back(static_cast<char>(byte));return 1;}
  size_t write(const uint8_t* buffer,size_t size) override {
    out.append(reinterpret_cast<const char*>(buffer),size);return size;
  }
};
inline std::string xmlEscape(std::string_view s) {
  std::string out;
  TxtToHtml::appendEscapedHtml(out,s);
  std::string quoted;
  for(char c:out) quoted+= c=='"'?std::string("&quot;"):std::string(1,c);
  return quoted;
}
struct ZipWriter {
  struct Entry {std::string name;uint32_t crc,size,offset;};
  std::string data;
  std::vector<Entry> entries;
  void le16(std::string& s,uint32_t v){s.push_back(char(v&255));s.push_back(char((v>>8)&255));}
  void le32(std::string& s,uint32_t v){le16(s,v&0xFFFF);le16(s,v>>16);}
  void add(const std::string& name,const std::string& body) {
    const Entry e{name,crc32(body),static_cast<uint32_t>(body.size()),static_cast<uint32_t>(data.size())};
    le32(data,0x04034b50u);le16(data,10);le16(data,0);le16(data,0);  // version, flags, stored
    le16(data,0);le16(data,0x21);                                    // 1980-01-01
    le32(data,e.crc);le32(data,e.size);le32(data,e.size);
    le16(data,static_cast<uint32_t>(name.size()));le16(data,0);
    data+=name;data+=body;
    entries.push_back(e);
  }
  std::string finish() {
    const uint32_t start=static_cast<uint32_t>(data.size());
    for(const auto& e:entries) {
      le32(data,0x02014b50u);le16(data,20);le16(data,10);le16(data,0);le16(data,0);
      le16(data,0);le16(data,0x21);
      le32(data,e.crc);le32(data,e.size);le32(data,e.size);
      le16(data,static_cast<uint32_t>(e.name.size()));le16(data,0);le16(data,0);
      le16(data,0);le16(data,0);le32(data,0);le32(data,e.offset);
      data+=e.name;
    }
    const uint32_t size=static_cast<uint32_t>(data.size())-start;
    le32(data,0x06054b50u);le16(data,0);le16(data,0);
    le16(data,static_cast<uint32_t>(entries.size()));le16(data,static_cast<uint32_t>(entries.size()));
    le32(data,size);le32(data,start);le16(data,0);
    return std::move(data);
  }
};
inline void startEpub(ZipWriter& zip) {
  zip.add("mimetype","application/epub+zip");
  zip.add("META-INF/container.xml",
      "<?xml version=\"1.0\"?>\n<container version=\"1.0\" "
      "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles>"
      "<rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/>"
      "</rootfiles></container>\n");
}
inline void finishEpub(ZipWriter& zip,std::string_view title,const std::string& id,
                       std::string_view language,const std::string& manifest,
                       const std::string& spine,const std::string& nav,std::string_view author={}) {
  const std::string t=xmlEscape(title);
  zip.add("OEBPS/content.opf",
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<package xmlns=\"http://www.idpf.org/2007/opf\" "
      "version=\"2.0\" unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\">"
      "<dc:title>"+t+"</dc:title><dc:identifier id=\"id\">"+id+"</dc:identifier>"
      "<dc:language>"+xmlEscape(language)+"</dc:language>"+
      (author.empty()?std::string{}:"<dc:creator>"+xmlEscape(author)+"</dc:creator>")+"</metadata><manifest>"
      "<item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"+manifest+
      "</manifest><spine toc=\"ncx\">"+spine+"</spine></package>\n");
  zip.add("OEBPS/toc.ncx",
      "<?xml version=\"1.0\" encoding=\"utf-8\"?>\n<ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" "
      "version=\"2005-1\"><head/><docTitle><text>"+t+"</text></docTitle><navMap>"+nav+"</navMap></ncx>\n");
}
}  // namespace txtpkg

// Build the EPUB for |text| (the .txt content). |name| is the file name, used
// for the title. Returns the package bytes.
inline std::string buildTxtEpub(std::string_view name,std::string_view text) {
  using namespace txtpkg;
  std::string converted;
  if(!validUtf8(text)){converted=latin1ToUtf8(text);text=converted;}
  std::string title(name);
  if(const size_t slash=title.find_last_of('/');slash!=std::string::npos)title.erase(0,slash+1);
  if(title.size()>4)title.resize(title.size()-4);
  if(!validUtf8(title))title=latin1ToUtf8(title);
  const auto parts=splitParts(text);
  ZipWriter zip;
  startEpub(zip);
  std::string manifest,spine,nav;
  for(size_t i=0;i<parts.size();++i) {
    char id[16];std::snprintf(id,sizeof(id),"t%04zu",i);
    const std::string file=std::string(id)+".xhtml";
    StringPrint html;
    (void)TxtToHtml::stream(file,parts[i],html);
    zip.add("OEBPS/"+file,html.out);
    manifest+="<item id=\""+std::string(id)+"\" href=\""+file+"\" media-type=\"application/xhtml+xml\"/>";
    spine+="<itemref idref=\""+std::string(id)+"\"/>";
    nav+="<navPoint id=\"n"+std::string(id)+"\" playOrder=\""+std::to_string(i+1)+
         "\"><navLabel><text>Part "+std::to_string(i+1)+"</text></navLabel><content src=\""+file+"\"/></navPoint>";
  }
  finishEpub(zip,title,"books-txt","und",manifest,spine,nav);
  return zip.finish();
}

// The cached package for |txtPath| under |cacheDir|, built if it is missing
// or the TXT changed (size or mtime are in the name). Empty on failure.
// Remove "<name>.part<pid>" files a killed build left for this target.
inline void removeStaleParts(const std::string& dir,const std::string& name) {
  DIR* d=opendir(dir.c_str());
  if(!d)return;
  const std::string prefix=name+".part";
  std::vector<std::string> stale;
  for(dirent* e;(e=readdir(d))!=nullptr;)
    if(std::string_view(e->d_name).rfind(prefix,0)==0)stale.push_back(dir+"/"+e->d_name);
  closedir(d);
  for(const auto& p:stale)std::remove(p.c_str());
}
inline std::string packageTxtBook(const std::string& txtPath,const std::string& cacheDir,
                                  std::string& error) {
  struct stat st{};
  if(::stat(txtPath.c_str(),&st)!=0 || !S_ISREG(st.st_mode)){error="TXT not found";return {};}
  if(static_cast<uint64_t>(st.st_size)>kTxtMaxBytes){error="TXT larger than 32 MB";return {};}
  uint64_t hash=14695981039346656037ull;
  const auto mix=[&](uint64_t v){for(int i=0;i<8;++i){hash^=(v>>(8*i))&255u;hash*=1099511628211ull;}};
  for(unsigned char b:txtPath){hash^=b;hash*=1099511628211ull;}
  mix(static_cast<uint64_t>(st.st_size));mix(static_cast<uint64_t>(st.st_mtime));
  mix(static_cast<uint64_t>(kTxtPackageVersion));
  ::mkdir(cacheDir.c_str(),0755);
  char leaf[48];std::snprintf(leaf,sizeof(leaf),"/txt-%016llx.epub",static_cast<unsigned long long>(hash));
  const std::string out=cacheDir+leaf;
  struct stat existing{};
  if(::stat(out.c_str(),&existing)==0 && S_ISREG(existing.st_mode) && existing.st_size>0)return out;
  removeStaleParts(cacheDir,out.substr(out.find_last_of('/')+1));
  FILE* in=std::fopen(txtPath.c_str(),"rb");
  if(!in){error="TXT could not be opened";return {};}
  std::string text(static_cast<size_t>(st.st_size),'\0');
  const size_t got=text.empty()?0:std::fread(&text[0],1,text.size(),in);
  std::fclose(in);
  text.resize(got);
  const std::string epub=buildTxtEpub(txtPath,text);
  const std::string tmp=out+".part"+std::to_string(static_cast<long>(::getpid()));
  FILE* f=std::fopen(tmp.c_str(),"wb");
  if(!f){error="cache not writable";return {};}
  const bool ok=std::fwrite(epub.data(),1,epub.size(),f)==epub.size();
  if(std::fclose(f)!=0 || !ok || std::rename(tmp.c_str(),out.c_str())!=0) {
    std::remove(tmp.c_str());error="cache write failed";return {};
  }
  return out;
}

}  // namespace books
