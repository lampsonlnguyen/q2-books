// SPDX-License-Identifier: MIT
#pragma once
// Portable, bounded SHA-256 (FIPS 180-4); no additional Q2 shared library.
// Used only for integrity checking of public, pinned release assets.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace books {
class FontPackSha256 {
  std::array<uint32_t,8> state_{{0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                               0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u}};
  std::array<uint8_t,64> block_{};
  uint64_t total_=0;
  size_t used_=0;
  static uint32_t rotate(uint32_t x,unsigned n) {return (x>>n)|(x<<(32-n));}
  void transform() {
    static constexpr uint32_t k[64]={
      0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
      0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
      0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
      0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
      0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
      0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
      0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
      0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u};
    uint32_t w[64]{};
    for(size_t i=0;i<16;++i) {
      const auto p=4*i;
      w[i]=(uint32_t(block_[p])<<24)|(uint32_t(block_[p+1])<<16)|
           (uint32_t(block_[p+2])<<8)|uint32_t(block_[p+3]);
    }
    for(size_t i=16;i<64;++i) {
      const auto x=w[i-15],y=w[i-2];
      w[i]=w[i-16]+(rotate(x,7)^rotate(x,18)^(x>>3))+w[i-7]+
           (rotate(y,17)^rotate(y,19)^(y>>10));
    }
    auto a=state_[0],b=state_[1],c=state_[2],d=state_[3],e=state_[4],f=state_[5],g=state_[6],h=state_[7];
    for(size_t i=0;i<64;++i) {
      const auto t1=h+(rotate(e,6)^rotate(e,11)^rotate(e,25))+((e&f)^(~e&g))+k[i]+w[i];
      const auto t2=(rotate(a,2)^rotate(a,13)^rotate(a,22))+((a&b)^(a&c)^(b&c));
      h=g;g=f;f=e;e=d+t1;d=c;c=b;b=a;a=t1+t2;
    }
    state_[0]+=a;state_[1]+=b;state_[2]+=c;state_[3]+=d;
    state_[4]+=e;state_[5]+=f;state_[6]+=g;state_[7]+=h;
  }
 public:
  void update(const void* data,size_t len) {
    const auto* bytes=static_cast<const uint8_t*>(data);
    total_+=len;
    while(len) {
      const auto n=std::min(len,block_.size()-used_);
      std::memcpy(block_.data()+used_,bytes,n);
      used_+=n;bytes+=n;len-=n;
      if(used_==block_.size()){transform();used_=0;}
    }
  }
  std::string hexDigest() const {
    auto copy=*this;
    const uint64_t bits=total_*8;
    const uint8_t one=0x80,zero=0;
    copy.update(&one,1);
    while(copy.used_!=56)copy.update(&zero,1);
    uint8_t length[8]{};
    for(unsigned i=0;i<8;++i)length[7-i]=static_cast<uint8_t>(bits>>(8*i));
    copy.update(length,sizeof length);
    constexpr char hex[]="0123456789abcdef";
    std::string out;
    out.reserve(64);
    for(uint32_t value:copy.state_)
      for(int shift=28;shift>=0;shift-=4)out+=hex[(value>>shift)&15];
    return out;
  }
};
} // namespace books
