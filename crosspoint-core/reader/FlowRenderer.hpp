// SPDX-License-Identifier: MIT
#pragma once
// Book-specific projection: firmware-style shared horizon and fading reflection,
// with aspect-preserving portrait artwork inspired by CrossInk's Lyra carousel.
#include "FlowCover.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace books {
namespace flow {
constexpr int width=375,height=320,top=76,bottom=240;
constexpr float reach=3.5f,eye=256.f;
constexpr int maxArtHeight=132;
inline uint32_t rgb(uint16_t p) {
  unsigned r=(p>>11)&31,g=(p>>5)&63,b=p&31;
  return ((r*255/31)<<16)|((g*255/63)<<8)|(b*255/31);
}
inline uint32_t mix(uint32_t a,uint32_t b,unsigned opacity) {
  opacity=std::min(256u,opacity);
  uint32_t out=0xff000000u;
  for(unsigned shift=0;shift<24;shift+=8)
    out|=((((a>>shift)&255u)*(256-opacity)+((b>>shift)&255u)*opacity)>>8)<<shift;
  return out;
}
struct Column {int source, y, h;};
inline bool project(const FlowCover& cover,float distance,int x,Column& col) {
  if(!cover.valid()||!std::isfinite(distance)||std::abs(distance)>=reach)return false;
  const float t=std::abs(distance),turn=std::min(t,1.f);
  const float angle=turn*1.57079632679f;
  const float c=std::cos(angle),s=std::sin(angle);
  const float center=128.f*turn+35.f*(t-turn),depth=eye+77.f*turn;
  const float sign=distance<0?-1.f:1.f;
  const float dx=(x+.5f-width*.5f)*sign;
  const float denominator=eye*c+dx*s;
  if(denominator<=.001f)return false;
  const float u=(dx*depth-eye*center)/denominator;
  const float scale=std::min(float(maxArtHeight)/cover.height,144.f/cover.width);
  const float w=cover.width*scale,h=cover.height*scale;
  const float sample=.5f+sign*u/w;
  const float z=depth-u*s;
  if(sample<0||sample>=1||z<=1)return false;
  col.source=std::min(int(cover.width)-1,int(sample*cover.width));
  col.h=std::max(1,int(h*eye/z+.5f));
  const int horizon=top+maxArtHeight*64/100;
  col.y=horizon-col.h*64/100;
  return true;
}
inline bool hit(const FlowCover& cover,float distance,int x,int y) {
  Column c{};
  return x>=0&&x<width&&project(cover,distance,x,c)&&y>=c.y&&y<c.y+c.h;
}
// Draw farthest covers first, then the center. Only the shelf band is touched.
inline void draw(uint32_t* frame,const FlowCover& cover,float distance,uint32_t paper) {
  if(!frame||!cover.valid()||!std::isfinite(distance)||std::abs(distance)>=reach)return;
  const float t=std::abs(distance);
  const unsigned brightness=unsigned(256.f-std::min(t,2.f)*64.f);
  const unsigned fade=t>3.f?unsigned((reach-t)*512.f):256u;
  for(int x=0;x<width;++x) {
    Column c{};
    if(!project(cover,distance,x,c))continue;
    const int end=c.y+c.h;
    for(int y=std::max(top,c.y);y<std::min(bottom,end);++y) {
      const unsigned sy=unsigned((y-c.y)*cover.height/c.h);
      auto& pixel=frame[y*width+x];
      pixel=mix(pixel,mix(0,rgb(cover.pixels[sy*cover.width+c.source]),brightness),fade);
    }
    const int reflection=std::max(1,c.h*32/maxArtHeight);
    for(int y=std::max(top,end);y<std::min(bottom,end+reflection);++y) {
      const unsigned sy=cover.height-1-unsigned((y-end)*cover.height/c.h);
      const unsigned opacity=unsigned((reflection-(y-end))*90/reflection)*brightness*fade/65536u;
      auto& pixel=frame[y*width+x];
      pixel=mix(pixel,mix(paper,rgb(cover.pixels[sy*cover.width+c.source]),brightness),opacity);
    }
  }
}
inline FlowCover placeholder(uint16_t color=0x39e7) {
  FlowCover cover;
  cover.width=104;cover.height=160;
  cover.pixels.assign(cover.width*cover.height,color);
  for(unsigned y=0;y<cover.height;++y)for(unsigned x=0;x<cover.width;++x)
    if(x<7||x+2>=cover.width||y<2||y+2>=cover.height)
      cover.pixels[y*cover.width+x]=0x2104;
  return cover;
}
} // namespace flow
} // namespace books
