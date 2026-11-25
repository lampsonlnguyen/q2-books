// SPDX-License-Identifier: MIT
#pragma once
// Bounded legacy PalmDOC/MOBI record reader. No DRM or KF8 decoder.
// Format reference: https://wiki.mobileread.com/wiki/MOBI
#include "TxtPackage.hpp"
#include "HtmlPackage.hpp"
#include <array>
#include <memory>
#include <map>
#include <set>
#include <charconv>
#include <fcntl.h>

namespace books {
namespace mobipkg {
constexpr size_t maxText=4u*1024*1024, maxRecord=128*1024, maxRecords=8192;
inline uint16_t be16(std::string_view s,size_t i) {
  return (uint16_t(static_cast<unsigned char>(s[i]))<<8)|static_cast<unsigned char>(s[i+1]);
}
inline uint32_t be32(std::string_view s,size_t i) {
  return (uint32_t(be16(s,i))<<16)|be16(s,i+2);
}
inline bool decodeRecord(std::string_view in,std::string& out,size_t limit) {
  // Back references are local to a record; never read from an earlier record.
  out.clear();
  for(size_t i=0;i<in.size();) {
    const unsigned c=static_cast<unsigned char>(in[i++]);
    if(c>=1 && c<=8) {
      if(c>in.size()-i || c>limit-out.size())return false;
      out.append(in.substr(i,c));i+=c;
    } else if(c<128) {
      if(out.size()==limit)return false;
      out.push_back(static_cast<char>(c));
    } else if(c>=192) {
      if(limit-out.size()<2)return false;
      out.push_back(' ');out.push_back(static_cast<char>(c^128));
    } else {
      if(i==in.size())return false;
      const unsigned pair=(c<<8)|static_cast<unsigned char>(in[i++]);
      const unsigned distance=(pair>>3)&0x7ff, count=(pair&7)+3;
      if(!distance || distance>out.size() || count>limit-out.size())return false;
      for(unsigned j=0;j<count;++j)out.push_back(out[out.size()-distance]);
    }
  }
  return true;
}
inline bool trimExtras(std::string_view& in,unsigned flags) {
  for(unsigned bits=flags>>1;bits;bits>>=1) {
    if(!(bits&1))continue;
    size_t value=0;bool stop=false;
    for(unsigned i=0;i<4 && i<in.size();++i) {
      const unsigned b=static_cast<unsigned char>(in[in.size()-1-i]);
      value|=size_t(b&127)<<(i*7);
      if(b&128){stop=true;break;}
    }
    if(!stop || !value || value>in.size())return false;
    in.remove_suffix(value);
  }
  if(flags&1) {
    if(in.empty())return false;
    const size_t n=(static_cast<unsigned char>(in.back())&3)+1;
    if(n>in.size())return false;
    in.remove_suffix(n);
  }
  return true;
}
inline std::string windows1252(std::string_view text) {
  constexpr std::array<unsigned,32> table={
    0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
    0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
    0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
    0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178};
  std::string out;
  for(unsigned char c:text) {
    unsigned cp=c>=128 && c<160?table[c-128]:c;
    if(cp<128)out.push_back(char(cp));
    else if(cp<2048){out.push_back(char(0xc0|(cp>>6)));out.push_back(char(0x80|(cp&63)));}
    else {out.push_back(char(0xe0|(cp>>12)));out.push_back(char(0x80|((cp>>6)&63)));out.push_back(char(0x80|(cp&63)));}
  }
  return out;
}
struct Book {
  std::string text,title,author,publisher,description,isbn,asin,language,cover;
  bool html=false,hasCover=false;
  std::vector<size_t> imageOffsets;
  size_t firstImage=0;
};
inline std::string metadataText(std::string_view raw,unsigned encoding,size_t limit=1000) {
  if(raw.size()>limit)return {};
  std::string result=encoding==1252?windows1252(raw):std::string(raw);
  if(result.size()>limit || !txtpkg::validUtf8(result))return {};
  for(char& c:result)if(static_cast<unsigned char>(c)<32)c=' ';
  const auto first=result.find_first_not_of(' ');
  if(first==result.npos)return {};
  return result.substr(first,result.find_last_not_of(' ')-first+1);
}
inline bool imageRecord(std::string_view s) {
  return s.starts_with("\x89PNG\r\n\x1a\n") || s.starts_with("\xff\xd8\xff");
}
// Translate legacy filepos links into synthetic fragment anchors. Targets
// are accepted only at markup boundaries or in text (not inside attributes).
// Scan quoted tags, comments and raw-text elements without interpreting scripts.
inline std::string rewriteFilePositions(std::string_view input) {
  struct Tag {size_t start,end;std::string name;size_t filepos=size_t(-1);};
  std::vector<Tag> tags;
  std::set<size_t> targets;
  const auto ws=[](char c){return c==' ' || c=='\t' || c=='\r' || c=='\n';};
  const auto lower=[](char c){return c>='A' && c<='Z'?char(c+32):c;};
  const auto namechar=[](char c){return (c>='A'&&c<='Z')||(c>='a'&&c<='z')||
       (c>='0'&&c<='9')||c==':'||c=='-'||c=='_';};
  for(size_t i=0;i<input.size();) {
    const auto p=input.find('<',i);
    if(p==input.npos)break;
    if(input.substr(p).starts_with("<!--")) {
      const auto end=input.find("-->",p+4);
      if(end==input.npos)break;
      tags.push_back({p,end+3,"!"});i=end+3;continue;
    }
    size_t end=p+1;char quote=0;
    for(;end<input.size();++end) {
      const char c=input[end];
      if(quote){if(c==quote)quote=0;}
      else if(c=='"'||c=='\'')quote=c;
      else if(c=='>')break;
    }
    if(end==input.size())break;
    size_t at=p+1;while(at<end&&ws(input[at]))++at;
    const bool closing=at<end&&input[at]=='/';
    if(closing)++at;
    std::string name;
    while(at<end&&namechar(input[at]))name+=lower(input[at++]);
    if(!closing&&(name=="script"||name=="style"||name=="title"||name=="textarea")) {
      std::string tail(input.substr(end+1));
      std::transform(tail.begin(),tail.end(),tail.begin(),lower);
      const auto close=tail.find("</"+name);
      if(close!=tail.npos) {
        const auto last=input.find('>',end+1+close);
        if(last!=input.npos){tags.push_back({p,last+1,"!"});i=last+1;continue;}
      }
    }
    Tag tag{p,end+1,name};
    if(!closing&&name=="a") {
      while(at<end) {
        while(at<end&&ws(input[at]))++at;
        const auto begin=at;
        while(at<end&&namechar(input[at]))++at;
        if(begin==at){++at;continue;}
        std::string key(input.substr(begin,at-begin));
        std::transform(key.begin(),key.end(),key.begin(),lower);
        while(at<end&&ws(input[at]))++at;
        if(at==end||input[at]!='=')continue;
        ++at;while(at<end&&ws(input[at]))++at;
        const char q=at<end&&(input[at]=='"'||input[at]=='\'')?input[at++]:0;
        const auto value=at;
        while(at<end&&(q?input[at]!=q:!ws(input[at])))++at;
        if(key=="filepos"&&at>value&&at-value<=10) {
          size_t pos=0;
          const auto parsed=std::from_chars(input.data()+value,input.data()+at,pos);
          if(parsed.ec==std::errc{}&&parsed.ptr==input.data()+at&&pos<input.size()&&targets.size()<4096) {
            tag.filepos=pos;targets.insert(pos);
          }
        }
        if(q&&at<end)++at;
      }
    }
    tags.push_back(std::move(tag));i=end+1;
    if(tags.size()>65536)return std::string(input);
  }
  if(targets.empty())return std::string(input);
  std::map<size_t,std::vector<size_t>> anchors;
  size_t ti=0;
  for(const auto target:targets) {
    while(ti<tags.size()&&tags[ti].end<=target)++ti;
    size_t pos=target;
    if(ti<tags.size()&&tags[ti].start<=pos) {
      if(tags[ti].name=="!")continue;
      pos=tags[ti].start;
    } else {
      // Never split a UTF-8 continuation byte or an HTML entity.
      while(pos>0&&(static_cast<unsigned char>(input[pos])&0xc0)==0x80)--pos;
      const auto amp=input.rfind('&',pos);
      if(amp!=input.npos&&pos-amp<=32) {
        const auto semi=input.find(';',amp);
        if(semi!=input.npos&&semi>=pos&&semi-amp<=32&&
           input.substr(amp,semi-amp).find_first_of("< \t\r\n")==input.npos)pos=amp;
      }
    }
    anchors[pos].push_back(target);
  }
  std::map<size_t,std::string> insertions;
  for(const auto& [pos,ids]:anchors)
    for(const auto id:ids)insertions[pos]+="<a id=\"mobi-pos-"+std::to_string(id)+"\"></a>";
  for(const auto& tag:tags)if(tag.filepos!=size_t(-1)) {
    // Add a synthetic href before existing attributes so the import parser
    // uses it in preference to a legacy fallback href.
    const auto start=tag.start+1;
    size_t at=start;while(at<tag.end&&ws(input[at]))++at;
    while(at<tag.end&&namechar(input[at]))++at;
    insertions[at]+=" href=\"#mobi-pos-"+std::to_string(tag.filepos)+"\"";
  }
  std::string out;
  size_t at=0;
  for(const auto& [pos,extra]:insertions) {
    out.append(input.substr(at,pos-at));out+=extra;at=pos;
    if(out.size()>maxText)return {};
  }
  if(input.size()-at>maxText-out.size())return {};
  out.append(input.substr(at));
  return out;
}
inline bool readBook(const std::string& path,Book& book,std::string& error,bool metadataOnly=false,bool artwork=true) {
  const auto fail=[&](const char* message){error=message;return false;};
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  if(fd<0)return fail("MOBI/PRC could not be opened");
  FILE* raw=::fdopen(fd,"rb");
  if(!raw){::close(fd);return fail("MOBI/PRC could not be opened");}
  std::unique_ptr<FILE,decltype(&std::fclose)> file(raw,&std::fclose);
  struct stat st{};
  if(::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<78 ||
      st.st_size>128ll*1024*1024)return fail("Invalid or oversized Palm database");
  const size_t size=static_cast<size_t>(st.st_size);
  const auto read=[&](size_t offset,size_t count,std::string& out) {
    if(offset>size || count>size-offset)return false;
    out.resize(count);
    return std::fseek(file.get(),static_cast<long>(offset),SEEK_SET)==0 &&
      std::fread(out.data(),1,count,file.get())==count;
  };
  std::string header,table,record;
  if(!read(0,78,header))return fail("Invalid Palm database header");
  const auto kind=std::string_view(header).substr(60,8);
  if((be16(header,32)&1) || (kind!="BOOKMOBI" && kind!="TEXtREAd"))
    return fail("Not a supported MOBI or PalmDOC ebook");
  const size_t count=be16(header,76);
  if(count<2 || count>maxRecords || !read(78,count*8,table))
    return fail("Invalid Palm record table");
  std::vector<size_t> offsets;
  offsets.reserve(count+1);
  for(size_t i=0;i<count;++i) {
    const size_t offset=be32(table,i*8);
    if(offset<78+count*8 || offset>=size || (!offsets.empty() && offset<=offsets.back()))
      return fail("Invalid Palm record offset");
    offsets.push_back(offset);
  }
  offsets.push_back(size);
  if(offsets[1]-offsets[0]<16 || offsets[1]-offsets[0]>maxRecord ||
     !read(offsets[0],offsets[1]-offsets[0],record))
    return fail("Invalid ebook header");
  const unsigned compression=be16(record,0), records=be16(record,8);
  const size_t expected=be32(record,4);
  if(be16(record,12)!=0)return fail("DRM-protected MOBI/PRC is unsupported");
  if(!metadataOnly && compression!=1 && compression!=2)return fail("Unsupported MOBI/PRC compression");
  if(!expected || (!metadataOnly && expected>maxText) || !records || records>=count)
    return fail("MOBI/PRC text exceeds the 4 MiB limit or has invalid records");
  unsigned encoding=1252,flags=0;
  size_t firstImage=count,coverOffset=size_t(-1),thumbOffset=size_t(-1);
  book={};
  if(kind=="BOOKMOBI") {
    if(record.size()<40 || record.compare(16,4,"MOBI")!=0)
      return fail("Invalid MOBI header");
    const size_t length=be32(record,20);
    if(length<24 || length>record.size()-16)return fail("Invalid MOBI header length");
    encoding=be32(record,28);
    const unsigned version=be32(record,36);
    if(version>=8)return fail("KF8-only MOBI is not supported yet");
    if(encoding!=1252 && encoding!=65001)return fail("Unsupported MOBI text encoding");
    if(version>=5 && length>=0xe4)flags=be16(record,0xf2);
    if(length>=76) {
      const size_t offset=be32(record,84),n=be32(record,88);
      if(n && n<=512 && offset<=record.size() && n<=record.size()-offset)
        book.title=metadataText(std::string_view(record).substr(offset,n),encoding);
    }
    // Offsets below are relative to the start of the complete record zero.
    if(length>=96)firstImage=be32(record,108);
    if(length>=116 && (be32(record,128)&0x40)) {
      const size_t exth=16+length;
      if(record.size()-exth>=12 && record.compare(exth,4,"EXTH")==0) {
        const size_t bytes=be32(record,exth+4),entries=be32(record,exth+8);
        if(bytes>=12 && bytes<=record.size()-exth && entries<=4096) {
          size_t at=exth+12;
          const size_t end=exth+bytes;
          for(size_t i=0;i<entries && end-at>=8;++i) {
            const unsigned type=be32(record,at);
            const size_t n=be32(record,at+4);
            if(n<8 || n>end-at)break;
            const auto value=std::string_view(record).substr(at+8,n-8);
            switch(type) {
              case 100: {
                const auto author=metadataText(value,encoding);
                if(!author.empty() && book.author.size()+author.size()+2<=1000) {
                  if(!book.author.empty())book.author+="; ";
                  book.author+=author;
                }
                break;
              }
              case 101: book.publisher=metadataText(value,encoding);break;
              case 103: book.description=metadataText(value,encoding,4096);break;
              case 104: book.isbn=metadataText(value,encoding);break;
              case 113: book.asin=metadataText(value,encoding);break;
              case 201: if(value.size()==4)coverOffset=be32(value,0);break;
              case 202: if(value.size()==4)thumbOffset=be32(value,0);break;
              case 503: {
                const auto title=metadataText(value,encoding);
                if(!title.empty())book.title=title;
                break;
              }
              case 524: book.language=metadataText(value,encoding,48);break;
            }
            at+=n;
          }
        }
      }
    }
    book.html=true;
  }
  if(book.title.empty()) {
    const auto slash=path.find_last_of('/');
    book.title=path.substr(slash==path.npos?0:slash+1);
    if(const auto dot=book.title.find_last_of('.');dot!=book.title.npos)book.title.resize(dot);
  }
  if(!book.html) {
    auto name=std::string_view(header).substr(0,32);
    const auto zero=name.find('\0');
    if(zero!=name.npos)name=name.substr(0,zero);
    const auto title=metadataText(name,1252);
    if(!title.empty())book.title=title;
  }
  if(firstImage<count && firstImage>records) {
    constexpr size_t maxCover=4u*1024u*1024u;
    for(size_t relative:{coverOffset,thumbOffset}) {
      if(relative>=count-firstImage)continue;
      const size_t index=firstImage+relative,n=offsets[index+1]-offsets[index];
      if(n<8 || n>maxCover)continue;
      std::string signature;
      if(!read(offsets[index],8,signature) || !imageRecord(signature))continue;
      book.hasCover=true;
      if(!artwork)break;
      if(read(offsets[index],n,book.cover))break;
      book.cover.clear();
    }
  }
  if(metadataOnly)return true;
  if(firstImage<count && firstImage>records) {
    book.firstImage=firstImage;
    book.imageOffsets=offsets;
  }
  book.text.reserve(expected);
  std::string decoded;
  for(size_t i=1;i<=records;++i) {
    const size_t n=offsets[i+1]-offsets[i];
    if(n>maxRecord || !read(offsets[i],n,record))return fail("Invalid ebook text record");
    std::string_view data(record);
    if(!trimExtras(data,flags))return fail("Malformed MOBI trailing data");
    // Each record expands into at most 64 KiB; permit padding at the last record only.
    constexpr size_t recordLimit=64*1024;
    if(compression==2) {
      if(!decodeRecord(data,decoded,recordLimit))return fail("Malformed PalmDOC compression");
      data=decoded;
    } else if(data.size()>recordLimit)return fail("Oversized ebook text record");
    const size_t remaining=expected-book.text.size();
    if(data.size()>remaining && i!=records)return fail("Invalid ebook text length");
    book.text.append(data.substr(0,remaining));
  }
  if(book.text.size()!=expected)return fail("Truncated ebook text");
  if(book.html) {
    // Legacy filepos refers to byte offsets in the original decompressed stream,
    // before Windows-1252 conversion. Resolve before changing encoding.
    book.text=rewriteFilePositions(book.text);
    if(book.text.empty())return fail("MOBI link conversion exceeded limits");
  }
  if(encoding==1252 && (book.html || !txtpkg::validUtf8(book.text)))
    book.text=windows1252(book.text);
  if(book.text.size()>maxText)return fail("Decoded ebook exceeds the 4 MiB limit");
  return true;
}
} // namespace mobipkg

inline std::string packageMobiBook(const std::string& path,const std::string& cacheDir,std::string& error) {
  mobipkg::Book book;
  if(!mobipkg::readBook(path,book,error,false,false))return {};
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  FILE* raw=fd<0?nullptr : ::fdopen(fd,"rb");
  if(fd>=0 && !raw)::close(fd);
  std::unique_ptr<FILE,decltype(&std::fclose)> file(raw,&std::fclose);
  const ImportedImageReader images=[&](size_t recindex,std::string& bytes) {
    if(!file || !recindex || book.imageOffsets.empty() ||
       recindex>book.imageOffsets.size()-1-book.firstImage)return false;
    const size_t index=book.firstImage+recindex-1;
    const size_t offset=book.imageOffsets[index],n=book.imageOffsets[index+1]-offset;
    if(n<8 || n>512*1024)return false;
    bytes.resize(n);
    return std::fseek(file.get(),static_cast<long>(offset),SEEK_SET)==0 &&
        std::fread(bytes.data(),1,n,file.get())==n && mobipkg::imageRecord(bytes);
  };
  return packageImportedBook(path,book.title,book.text,book.html,cacheDir,error,
                             book.author,book.language,images,book.html);
}
} // namespace books
