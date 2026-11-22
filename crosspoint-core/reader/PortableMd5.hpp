// SPDX-License-Identifier: MIT
#pragma once
// Compact RFC 1321 MD5 implementation used solely for the KOReader document
// identity and KOSync's required x-auth-key digest. MD5 is NOT used for
// integrity/security decisions; HTTPS is required for untrusted networks.
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <sys/stat.h>

namespace books {
class PortableMd5 {
  uint32_t state_[4]={0x67452301u,0xefcdab89u,0x98badcfeu,0x10325476u};
  uint64_t bytes_=0;
  std::array<uint8_t,64> pending_{};
  size_t used_=0;
  static uint32_t rotate(uint32_t value,unsigned count) {
    return (value<<count)|(value>>(32-count));
  }
  void block(const uint8_t* data) {
    constexpr unsigned shifts[64]={
      7,12,17,22, 7,12,17,22, 7,12,17,22, 7,12,17,22,
      5,9,14,20, 5,9,14,20, 5,9,14,20, 5,9,14,20,
      4,11,16,23, 4,11,16,23, 4,11,16,23, 4,11,16,23,
      6,10,15,21, 6,10,15,21, 6,10,15,21, 6,10,15,21};
    constexpr uint32_t constants[64]={
      0xd76aa478u,0xe8c7b756u,0x242070dbu,0xc1bdceeeu,
      0xf57c0fafu,0x4787c62au,0xa8304613u,0xfd469501u,
      0x698098d8u,0x8b44f7afu,0xffff5bb1u,0x895cd7beu,
      0x6b901122u,0xfd987193u,0xa679438eu,0x49b40821u,
      0xf61e2562u,0xc040b340u,0x265e5a51u,0xe9b6c7aau,
      0xd62f105du,0x02441453u,0xd8a1e681u,0xe7d3fbc8u,
      0x21e1cde6u,0xc33707d6u,0xf4d50d87u,0x455a14edu,
      0xa9e3e905u,0xfcefa3f8u,0x676f02d9u,0x8d2a4c8au,
      0xfffa3942u,0x8771f681u,0x6d9d6122u,0xfde5380cu,
      0xa4beea44u,0x4bdecfa9u,0xf6bb4b60u,0xbebfbc70u,
      0x289b7ec6u,0xeaa127fau,0xd4ef3085u,0x04881d05u,
      0xd9d4d039u,0xe6db99e5u,0x1fa27cf8u,0xc4ac5665u,
      0xf4292244u,0x432aff97u,0xab9423a7u,0xfc93a039u,
      0x655b59c3u,0x8f0ccc92u,0xffeff47du,0x85845dd1u,
      0x6fa87e4fu,0xfe2ce6e0u,0xa3014314u,0x4e0811a1u,
      0xf7537e82u,0xbd3af235u,0x2ad7d2bbu,0xeb86d391u};
    uint32_t words[16]{};
    for(unsigned i=0;i<16;++i)
      for(unsigned b=0;b<4;++b)
        words[i]|=static_cast<uint32_t>(data[i*4+b])<<(b*8);
    uint32_t a=state_[0],b=state_[1],c=state_[2],d=state_[3];
    for(unsigned i=0;i<64;++i) {
      uint32_t function=0,index=0;
      if(i<16){function=(b&c)|(~b&d);index=i;}
      else if(i<32){function=(d&b)|(~d&c);index=(5*i+1)%16;}
      else if(i<48){function=b^c^d;index=(3*i+5)%16;}
      else {function=c^(b|~d);index=(7*i)%16;}
      const uint32_t saved=d;
      d=c;c=b;
      b+=rotate(a+function+constants[i]+words[index],shifts[i]);
      a=saved;
    }
    state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
  }
public:
  void update(const void* input,size_t length) {
    const auto* bytes=static_cast<const uint8_t*>(input);
    bytes_+=length;
    while(length) {
      const size_t take=std::min(length,pending_.size()-used_);
      std::memcpy(pending_.data()+used_,bytes,take);
      used_+=take;bytes+=take;length-=take;
      if(used_==pending_.size()) {block(pending_.data());used_=0;}
    }
  }
  std::string finish() {
    const uint64_t bits=bytes_*8u;
    const uint8_t pad=0x80;
    update(&pad,1);
    const uint8_t zero=0;
    while(used_!=56)update(&zero,1);
    uint8_t length[8]{};
    for(unsigned i=0;i<8;++i)
      length[i]=static_cast<uint8_t>((bits>>(i*8))&0xff);
    update(length,8);
    constexpr char hex[]="0123456789abcdef";
    std::string result;
    result.reserve(32);
    for(uint32_t value:state_)
      for(unsigned i=0;i<4;++i) {
        const unsigned byte=(value>>(i*8))&0xff;
        result+=hex[byte>>4];result+=hex[byte&15];
      }
    return result;
  }
};
inline std::string md5String(const std::string& input) {
  PortableMd5 md5;md5.update(input.data(),input.size());return md5.finish();
}
inline std::string koReaderPartialMd5(const std::string& book) {
  struct stat st{};
  if(::lstat(book.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size<=0)return {};
  FILE* input=std::fopen(book.c_str(),"rb");
  if(!input)return {};
  const uint64_t filesize=static_cast<uint64_t>(st.st_size);
  PortableMd5 digest;
  // Algorithm from original CrossPoint KOReaderDocumentId.cpp and KOReader:
  // 1 KiB at 0, 1024, 4096, 16384 ... 1073741824 (skip offsets past EOF).
  uint64_t offset=0;
  for(unsigned index=0;index<12;++index) {
    if(index==0)offset=0;
    else offset=1024ull<<(2*(index-1));
    if(offset>=filesize)continue;
    if(::fseeko(input,static_cast<off_t>(offset),SEEK_SET)!=0) {
      std::fclose(input);return {};
    }
    uint8_t data[1024]{};
    const size_t want=static_cast<size_t>(
        std::min<uint64_t>(sizeof(data),filesize-offset));
    const size_t read=std::fread(data,1,want,input);
    if(read!=want) {std::fclose(input);return {};}
    digest.update(data,read);
  }
  std::fclose(input);
  return digest.finish();
}
} // namespace books
