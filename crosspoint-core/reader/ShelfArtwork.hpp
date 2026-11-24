// SPDX-License-Identifier: MIT
#pragma once

#include "FlowRenderer.hpp"
#include "ShelfCover.hpp"

namespace books::shelf_art {
constexpr int width=104,height=160;

inline uint16_t rgb565(uint32_t color) {
  return uint16_t(((color>>8)&0xf800u)|((color>>5)&0x07e0u)|((color>>3)&0x001fu));
}

class Canvas {
 public:
  explicit Canvas(uint32_t paper):cover_{width,height,std::vector<uint16_t>(width*height,rgb565(paper))} {}
  void rect(int x,int y,int w,int h,uint32_t color,int radius=0) {
    const auto ink=rgb565(color);
    for(int row=0;row<h;++row)for(int col=0;col<w;++col) {
      const int dx=std::max({radius-col,0,col-(w-1-radius)});
      const int dy=std::max({radius-row,0,row-(h-1-radius)});
      if(dx*dx+dy*dy<=radius*radius)
        cover_.pixels[size_t(y+row)*width+x+col]=ink;
    }
  }
  void disc(int x,int y,int radius,uint32_t color) {
    const auto ink=rgb565(color);
    for(int dy=-radius;dy<=radius;++dy)for(int dx=-radius;dx<=radius;++dx)
      if(dx*dx+dy*dy<=radius*radius)cover_.pixels[size_t(y+dy)*width+x+dx]=ink;
  }
  FlowCover finish() {return std::move(cover_);}
 private:
  FlowCover cover_;
};

inline const FlowCover& defaultCover() {
  static const auto cover=[] {
    Canvas art(0xeee7d8);
    art.rect(15,16,76,132,0xc6bca8,7);
    art.rect(11,12,76,132,0x234850,7);
    art.rect(11,15,10,126,0x17333b,3);
    art.rect(25,18,57,120,0x71a19c,4);
    art.rect(27,20,53,116,0x234850,3);
    art.rect(34,43,39,40,0xf0d7a0,4);
    art.rect(38,47,31,32,0x386168,2);
    art.rect(42,55,23,3,0xf0d7a0);
    art.rect(42,62,23,3,0xf0d7a0);
    art.rect(42,69,16,3,0xf0d7a0);
    art.rect(36,109,35,2,0x9cb5a9);
    art.rect(43,116,21,2,0x9cb5a9);
    return art.finish();
  }();
  return cover;
}

inline const FlowCover& guideCover() {
  static const auto cover=[] {
    Canvas art(0x24566e);
    art.rect(6,6,92,148,0x34758a,9);
    art.rect(12,20,84,126,0x183949,11);
    art.rect(9,15,86,126,0xe6e5dc,11);
    art.rect(13,22,78,55,0x1b303e,6);
    art.rect(15,27,74,45,0x0d1c29,4);
    // The word Guide is drawn into the player's display at a fixed size.
    constexpr uint8_t glyphs[5][6]={
      {6,9,8,11,9,7}, // G
      {0,0,9,9,9,7},  // u
      {4,0,12,4,4,14}, // i
      {1,1,7,9,9,7},  // d
      {0,0,6,15,8,7}, // e
    };
    for(int letter=0;letter<5;++letter)
      for(int y=0;y<6;++y)
        for(int x=0;x<4;++x)
          if(glyphs[letter][y]&(1u<<(3-x)))
            art.rect(16+(letter*5+x)*3,40+y*3,3,3,0xffead0);
    art.disc(50,108,25,0xb6bebc);
    art.disc(50,108,23,0xf7f5eb);
    art.disc(50,108,9,0xb6bebc);
    art.disc(50,108,8,0xe5e7de);
    art.rect(45,90,10,2,0x526774,1);
    art.rect(48,124,4,2,0x526774);
    art.rect(33,106,3,4,0x526774);
    art.rect(64,106,3,4,0x526774);
    return art.finish();
  }();
  return cover;
}

inline const ShelfCover& defaultThumbnail() {
  static const auto thumb=[] {
    const auto& source=defaultCover();
    PngPreview gray;
    gray.width=source.width;
    gray.height=source.height;
    gray.gray.reserve(source.pixels.size());
    for(const auto pixel:source.pixels) {
      const auto rgb=flow::rgb(pixel);
      gray.gray.push_back(static_cast<uint8_t>(
          (77*((rgb>>16)&255u)+150*((rgb>>8)&255u)+29*(rgb&255u)+128)>>8));
    }
    ShelfCover cover;
    (void)makeShelfCover(gray,cover);
    return cover;
  }();
  return thumb;
}
} // namespace books::shelf_art
