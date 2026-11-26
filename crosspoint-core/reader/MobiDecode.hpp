// SPDX-License-Identifier: MIT
#pragma once
// Legacy Palm database reader. Source offsets remain intact until packaging.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace books::mobipkg {
constexpr size_t maxText=8u*1024*1024, maxRecord=128u*1024, maxRecords=16384;
constexpr size_t maxImage=1024u*1024;
inline unsigned byte(std::string_view s,size_t i) {return static_cast<unsigned char>(s[i]);}
inline uint16_t be16(std::string_view s,size_t i) {return (byte(s,i)<<8)|byte(s,i+1);}
inline uint32_t be32(std::string_view s,size_t i) {return (uint32_t(be16(s,i))<<16)|be16(s,i+2);}
inline bool xmlChar(unsigned c) {return c==9 || c==10 || c==13 || (c>=32 && c<=0xd7ff) || (c>=0xe000 && c<=0xfffd) || (c>=0x10000 && c<=0x10ffff);}
inline void utf8(std::string& out,unsigned c) {
  if(!xmlChar(c))c=0xfffd;
  if(c<128)out+=char(c);
  else if(c<2048){out+=char(0xc0|(c>>6));out+=char(0x80|(c&63));}
  else if(c<65536){out+=char(0xe0|(c>>12));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
  else {out+=char(0xf0|(c>>18));out+=char(0x80|((c>>12)&63));out+=char(0x80|((c>>6)&63));out+=char(0x80|(c&63));}
}
inline unsigned codepoint(std::string_view s,size_t& i,bool latin,bool& valid) {
  const unsigned c=byte(s,i++);
  if(c<128)return c;
  if(latin) {
    constexpr unsigned cp[]={0x20ac,0xfffd,0x201a,0x192,0x201e,0x2026,0x2020,0x2021,0x2c6,0x2030,0x160,0x2039,0x152,0xfffd,0x17d,0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,0x2dc,0x2122,0x161,0x203a,0x153,0xfffd,0x17e,0x178};
    return c<160?cp[c-128]:c;
  }
  const unsigned n=c>=0xc2&&c<=0xdf?1:c>=0xe0&&c<=0xef?2:c>=0xf0&&c<=0xf4?3:0;
  unsigned v=c&((1u<<(6-n))-1);
  if(!n || n>s.size()-i){valid=false;return 0xfffd;}
  for(unsigned k=0;k<n;++k){const unsigned b=byte(s,i+k);if((b&0xc0)!=0x80){valid=false;return 0xfffd;}v=(v<<6)|(b&63);}
  i+=n;
  if(v<(n==1?0x80u:n==2?0x800u:0x10000u) || v>0x10ffff || (v>=0xd800&&v<=0xdfff)){valid=false;return 0xfffd;}
  return v;
}
inline bool validUtf8(std::string_view s) {bool valid=true;for(size_t i=0;i<s.size()&&valid;)(void)codepoint(s,i,false,valid);return valid;}
inline std::string metadataText(std::string_view s,unsigned encoding,size_t limit=4096) {
  if(s.size()>limit)return {};
  std::string out;bool valid=true;
  for(size_t i=0;i<s.size();){const unsigned c=codepoint(s,i,encoding==1252,valid);utf8(out,c<32?' ':c);}
  if(!valid || out.size()>limit)return {};
  const auto first=out.find_first_not_of(' ');return first==out.npos?std::string{}:out.substr(first,out.find_last_not_of(' ')-first+1);
}
inline bool decodeRecord(std::string_view in,std::string& out) {
  constexpr size_t limit=64*1024;out.clear();
  for(size_t i=0;i<in.size();) {
    const unsigned c=byte(in,i++);
    if(c>=1&&c<=8){if(c>in.size()-i||c>limit-out.size())return false;out.append(in.substr(i,c));i+=c;}
    else if(c<128){if(out.size()==limit)return false;out+=char(c);}
    else if(c>=192){if(limit-out.size()<2)return false;out+=' ';out+=char(c^128);}
    else {if(i==in.size())return false;const unsigned pair=(c<<8)|byte(in,i++),distance=(pair>>3)&2047,count=(pair&7)+3;
      if(!distance||distance>out.size()||count>limit-out.size())return false;
      for(unsigned k=0;k<count;++k)out+=out[out.size()-distance];}
  }
  return true;
}
inline bool trimExtras(std::string_view& in,unsigned flags) {
  for(unsigned bits=flags>>1;bits;bits>>=1)if(bits&1){
    size_t n=0;bool stop=false;
    for(unsigned j=0;j<4&&j<in.size();++j){const unsigned c=byte(in,in.size()-1-j);n|=size_t(c&127)<<(7*j);if(c&128){stop=true;break;}}
    if(!stop||!n||n>in.size())return false;
    in.remove_suffix(n);
  }
  if(flags&1){if(in.empty())return false;const size_t n=(byte(in,in.size()-1)&3)+1;if(n>in.size())return false;in.remove_suffix(n);}
  return true;
}
struct CloseFile {void operator()(FILE* f) const {if(f)std::fclose(f);}};
using File=std::unique_ptr<FILE,CloseFile>;
inline File openSource(const std::string& path) {
  const int fd=::open(path.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC);
  FILE* f=fd<0?nullptr: ::fdopen(fd,"rb");if(fd>=0&&!f)::close(fd);return File(f);
}
inline bool readAt(FILE* f,size_t at,size_t n,std::string& out) {
  out.resize(n);return f&&std::fseek(f,static_cast<long>(at),SEEK_SET)==0&&std::fread(out.data(),1,n,f)==n;
}
inline int imageType(std::string_view s) {
  if(s.starts_with("\x89PNG\r\n\x1a\n"))return 1;
  if(s.starts_with("\xff\xd8\xff"))return 2;
  if(s.starts_with("GIF87a")||s.starts_with("GIF89a"))return 3;
  return 0;
}
struct Book {
  std::string text,title,author,publisher,description,isbn,asin,language,cover;
  unsigned encoding=65001;bool html=false,hasCover=false;
  size_t firstImage=0,coverIndex=0;
  std::vector<size_t> offsets;
};
inline bool imageBytes(FILE* f,const Book& b,size_t recindex,std::string& out) {
  if(!recindex||!b.firstImage||b.offsets.empty()||recindex>b.offsets.size()-1-b.firstImage)return false;
  const size_t i=b.firstImage+recindex-1,n=b.offsets[i+1]-b.offsets[i];
  return n>=8&&n<=maxImage&&readAt(f,b.offsets[i],n,out)&&imageType(out);
}
inline bool readBook(const std::string& path,Book& book,std::string& error,bool metadataOnly=false,bool artwork=true) {
  const auto fail=[&](const char* e){error=e;return false;};
  auto file=openSource(path);struct stat st{};
  if(!file||::fstat(::fileno(file.get()),&st)!=0||!S_ISREG(st.st_mode)||st.st_size<78||st.st_size>128ll*1024*1024)return fail("Invalid Palm database");
  const size_t size=static_cast<size_t>(st.st_size);std::string h,table,record;
  if(!readAt(file.get(),0,78,h))return fail("Invalid Palm header");
  const auto kind=std::string_view(h).substr(60,8);
  if((be16(h,32)&1)||(kind!="BOOKMOBI"&&kind!="TEXtREAd"))return fail("Not a supported Palm ebook");
  const size_t count=be16(h,76);
  if(count<2||count>maxRecords||78+count*8>size||!readAt(file.get(),78,count*8,table))return fail("Invalid record table");
  book={};book.offsets.reserve(count+1);
  for(size_t i=0;i<count;++i){const size_t off=be32(table,i*8);if(off<78+count*8||off>size||(!book.offsets.empty()&&off<book.offsets.back()))return fail("Invalid record offset");book.offsets.push_back(off);}
  book.offsets.push_back(size);const auto& off=book.offsets;
  if(off[1]-off[0]<16||off[1]-off[0]>maxRecord||!readAt(file.get(),off[0],off[1]-off[0],record))return fail("Invalid ebook header");
  const unsigned compression=be16(record,0),records=be16(record,8);const size_t expected=be32(record,4);
  if(be16(record,12))return fail("DRM-protected MOBI/PRC");
  if(!metadataOnly&&compression!=1&&compression!=2)return fail("Unsupported MOBI compression");
  if(!expected||!records||records>=count||(!metadataOnly&&expected>maxText))return fail("Invalid records or decoded text exceeds 8 MiB");
  unsigned flags=0;size_t cover=size_t(-1),thumb=size_t(-1);
  book.html=kind=="BOOKMOBI";
  if(book.html){
    if(record.size()<40||record.compare(16,4,"MOBI"))return fail("Invalid MOBI header");
    const size_t length=be32(record,20);if(length<24||length>record.size()-16)return fail("Invalid MOBI header length");
    book.encoding=be32(record,28);if(book.encoding!=1252&&book.encoding!=65001)return fail("Unsupported text encoding");
    const unsigned version=be32(record,36);if(version>=8)return fail("KF8-only MOBI unsupported");
    if(version>=5&&length>=228)flags=be16(record,242);
    if(length>=76){const size_t a=be32(record,84),n=be32(record,88);if(a<=record.size()&&n<=record.size()-a)book.title=metadataText(std::string_view(record).substr(a,n),book.encoding,1024);}
    if(length>=96){const size_t first=be32(record,108);if(first>records&&first<count)book.firstImage=first;}
    if(length>=116&&(be32(record,128)&64)){
      const size_t e=16+length;if(record.size()-e>=12&&record.compare(e,4,"EXTH")==0){
        const size_t bytes=be32(record,e+4),entries=be32(record,e+8);
        if(bytes>=12&&bytes<=record.size()-e&&entries<=4096){size_t a=e+12,end=e+bytes;
          for(size_t i=0;i<entries&&end-a>=8;++i){const unsigned type=be32(record,a);const size_t n=be32(record,a+4);if(n<8||n>end-a)break;
            const auto value=std::string_view(record).substr(a+8,n-8);const auto text=[&]{return metadataText(value,book.encoding);};
            switch(type){case 100:{const auto author=text();if(!author.empty()&&book.author.size()+author.size()+2<=1024){if(!book.author.empty())book.author+="; ";book.author+=author;}break;}
              case 101:book.publisher=text();break;case 103:book.description=text();break;case 104:book.isbn=text();break;case 113:book.asin=text();break;
              case 201:if(value.size()==4)cover=be32(value,0);break;case 202:if(value.size()==4)thumb=be32(value,0);break;
              case 503:{const auto title=text();if(!title.empty())book.title=title;break;}case 524:book.language=metadataText(value,book.encoding,48);break;}
            a+=n;
          }
        }
      }
    }
  } else {auto name=std::string_view(h).substr(0,32);name=name.substr(0,name.find('\0'));book.title=metadataText(name,validUtf8(name)?65001:1252,1024);}
  if(book.title.empty()){const auto slash=path.find_last_of('/');book.title=path.substr(slash==path.npos?0:slash+1);const auto dot=book.title.find_last_of('.');if(dot!=book.title.npos)book.title.resize(dot);}
  if(book.firstImage){
    // A declared EXTH cover is preferred. Old generators omit it; the first image is then a reasonable shelf thumbnail.
    for(size_t rel:{cover,thumb,size_t(0)})if(rel<count-book.firstImage){
      const size_t i=book.firstImage+rel,n=off[i+1]-off[i];std::string sig;
      if(n>=8&&n<=maxImage&&readAt(file.get(),off[i],8,sig)&&imageType(sig)){
        book.hasCover=true;book.coverIndex=rel+1;if(artwork)(void)imageBytes(file.get(),book,book.coverIndex,book.cover);break;}
    }
  }
  if(metadataOnly)return true;
  book.text.reserve(expected);std::string decoded;decoded.reserve(4096);
  for(size_t i=1;i<=records;++i){const size_t n=off[i+1]-off[i];if(n>maxRecord||!readAt(file.get(),off[i],n,record))return fail("Invalid text record");
    std::string_view data(record);if(!trimExtras(data,flags))return fail("Malformed MOBI trailing data");
    if(compression==2){if(!decodeRecord(data,decoded))return fail("Malformed PalmDOC compression");data=decoded;}
    if(data.size()>64*1024)return fail("Oversized text record");
    const size_t remaining=expected-book.text.size();if(data.size()>remaining&&i!=records)return fail("Invalid text length");book.text.append(data.substr(0,remaining));
  }
  if(book.text.size()!=expected)return fail("Truncated ebook text");
  if(!book.html)book.encoding=validUtf8(book.text)?65001:1252;
  return true;
}
} // namespace books::mobipkg
