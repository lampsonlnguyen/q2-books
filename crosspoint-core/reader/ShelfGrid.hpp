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
  if(!cover.valid())return;
  int w=width,h=height;
  if(cover.width*h>cover.height*w)h=std::max(1,int(cover.height*w/cover.width));
  else w=std::max(1,int(cover.width*h/cover.height));
  const int x=left+(width-w)/2,y=top+(height-h)/2;
  for(int py=0;py<h;++py) {
    const auto* source=cover.pixels.data()+size_t(py*cover.height/h)*cover.width;
    auto* target=frame+(y+py)*flow::width+x;
    for(int px=0;px<w;++px)target[px]=0xff000000u|flow::rgb(source[px*cover.width/w]);
  }
}
inline void drawCover(uint32_t* frame,const FlowCover& cover,Tile cell) {
  drawThumbnail(frame,cover,cell.x+9,cell.y+4,tileWidth-18,coverHeight);
}
} // namespace books::grid
