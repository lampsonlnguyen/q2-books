// SPDX-License-Identifier: MIT
#pragma once

#include "FlowRenderer.hpp"

namespace books::grid {
constexpr int columns=3, rows=2, pageSize=columns*rows;
constexpr int left=20, top=76, tileWidth=110, tileHeight=94, gap=2;
constexpr int coverHeight=65;
struct Tile {int x,y;};
inline Tile tile(int cell) {
  return {left+(cell%columns)*(tileWidth+gap),top+(cell/columns)*tileHeight};
}
inline int hit(int x,int y) {
  if(x<left || y<top || y>=top+rows*tileHeight)return -1;
  const int col=(x-left)/(tileWidth+gap);
  if(col>=columns || (x-left)%(tileWidth+gap)>=tileWidth)return -1;
  return (y-top)/tileHeight*columns+col;
}
inline size_t pageStart(size_t selected) {return selected/pageSize*pageSize;}
inline void drawThumbnail(uint32_t* frame,const FlowCover& cover,int left,int top,int width,int height) {
  if(!frame || width<=0 || height<=0 || !cover.valid())return;
  int w=width,h=height;
  if(cover.width*h>cover.height*w)h=std::max(1,int(cover.height*w/cover.width));
  else w=std::max(1,int(cover.width*h/cover.height));
  const int x=left+(width-w)/2,y=top+(height-h)/2;
  const auto coordinate=[](int pixel,int length,int source) {
    return std::clamp((2*pixel+1)*source*128/length-128,0,(source-1)*256);
  };
  for(int py=0;py<h;++py) {
    const int sy=coordinate(py,h,cover.height);
    const auto* upper=cover.pixels.data()+size_t(sy/256)*cover.width;
    const auto* lower=sy/256+1<int(cover.height)?upper+cover.width:upper;
    auto* target=frame+(y+py)*flow::width+x;
    for(int px=0;px<w;++px) {
      const int sx=coordinate(px,w,cover.width);
      const int left=sx/256,right=std::min(left+1,int(cover.width)-1);
      const uint32_t a=flow::mix(flow::rgb(upper[left]),flow::rgb(upper[right]),sx%256);
      const uint32_t b=flow::mix(flow::rgb(lower[left]),flow::rgb(lower[right]),sx%256);
      target[px]=flow::mix(a,b,sy%256);
    }
  }
}
inline void drawCover(uint32_t* frame,const FlowCover& cover,Tile cell) {
  drawThumbnail(frame,cover,cell.x+9,cell.y+4,tileWidth-18,coverHeight);
}
} // namespace books::grid
