// SPDX-License-Identifier: MIT
#pragma once
// Bounded legacy PalmDOC/MOBI record reader. No DRM or KF8 decoder.
// Format reference: https://wiki.mobileread.com/wiki/MOBI
#include "TxtPackage.hpp"
#include "HtmlPackage.hpp"
#include <array>
#include <memory>
#include <fcntl.h>

namespace books {
namespace mobipkg {
constexpr size_t maxText=1024*1024, maxRecord=128*1024, maxRecords=8192;
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
struct Book {std::string text,title;bool html=false;};
inline bool readBook(const std::string& path,Book& book,std::string& error) {
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
  if(compression!=1 && compression!=2)return fail("Unsupported MOBI/PRC compression");
  if(!expected || expected>maxText || !records || records>=count)
    return fail("MOBI/PRC text exceeds the 1 MiB limit or has invalid records");
  unsigned encoding=1252,flags=0;
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
        book.title=record.substr(offset,n);
    }
    book.html=true;
  }
  if(book.title.empty()) {
    const auto slash=path.find_last_of('/');
    book.title=path.substr(slash==path.npos?0:slash+1);
    if(const auto dot=book.title.find_last_of('.');dot!=book.title.npos)book.title.resize(dot);
  } else if(encoding==1252)book.title=windows1252(book.title);
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
  if(encoding==1252 && (book.html || !txtpkg::validUtf8(book.text)))
    book.text=windows1252(book.text);
  if(book.text.size()>maxText)return fail("Decoded ebook exceeds the 1 MiB limit");
  return true;
}
} // namespace mobipkg

inline std::string packageMobiBook(const std::string& path,const std::string& cacheDir,std::string& error) {
  mobipkg::Book book;
  if(!mobipkg::readBook(path,book,error))return {};
  return packageImportedBook(path,book.title,book.text,book.html,cacheDir,error);
}
} // namespace books
