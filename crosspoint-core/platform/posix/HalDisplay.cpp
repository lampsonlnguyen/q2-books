// SPDX-License-Identifier: MIT
#include "HalDisplay.h"
#include "Q2PagePresenter.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

HalDisplay::HalDisplay() : raster_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  inkRaster_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  grayLsb_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  grayMsb_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  alpha_(new (std::nothrow) uint8_t[PIXELS]),
  expect_(new (std::nothrow) uint8_t[PIXELS]),
  shown_(new (std::nothrow) uint8_t[PIXELS]) {
  if(expect_)std::memset(expect_.get(),0,PIXELS);
  clearScreen();
}
HalDisplay::~HalDisplay() = default;
void HalDisplay::begin(bool) { if (!raster_) return; }
void HalDisplay::setGrayPage(const uint8_t* bwPage) const {
  if(!bwPage||!grayLsb_||!grayMsb_||!alpha_||!expect_)return;
  using books::Q2Display;
  for(int y=0;y<DISPLAY_HEIGHT;++y)
    for(int x=0;x<DISPLAY_WIDTH;++x) {
      const unsigned ink=1u-Q2Display::bitAt(bwPage,x,y);  // CrossPoint: 1 = paper
      const size_t i=size_t(y)*DISPLAY_WIDTH+unsigned(x);
      alpha_[i]=Q2Display::planeAlpha(ink,Q2Display::bitAt(grayLsb_.get(),x,y),
                                       Q2Display::bitAt(grayMsb_.get(),x,y));
      expect_[i]=ink?Q2Display::kExpectInk:Q2Display::kExpectPaper;
    }
}
void HalDisplay::drawCoverage(int x0,int y0,int w,int h,const uint8_t* cov,int stride) const {
  if(!cov||!raster_||!alpha_||!expect_||loaned_)return;
  using books::Q2Display;
  for(int gy=0;gy<h;++gy) {
    const int y=y0+gy;
    if(y<0||y>=DISPLAY_HEIGHT)continue;
    for(int gx=0;gx<w;++gx) {
      const int x=x0+gx;
      const uint8_t a=cov[gy*stride+gx];
      if(!a||x<0||x>=DISPLAY_WIDTH)continue;
      const size_t i=size_t(y)*DISPLAY_WIDTH+unsigned(x);
      uint8_t& byte=raster_[size_t(y)*DISPLAY_WIDTH_BYTES+unsigned(x)/8];
      const uint8_t mask=static_cast<uint8_t>(0x80u>>(x&7));
      const bool inkNow=!(byte&mask);
      const uint8_t e=expect_[i];
      const uint8_t prior=(e!=Q2Display::kExpectNone && inkNow==(e==Q2Display::kExpectInk))
          ?alpha_[i]:static_cast<uint8_t>(inkNow?255:0);
      const uint8_t next=std::max(prior,a);
      if(next>=128)byte&=static_cast<uint8_t>(~mask);else byte|=mask;
      alpha_[i]=next;
      expect_[i]=next>=128?Q2Display::kExpectInk:Q2Display::kExpectPaper;
    }
  }
}
void HalDisplay::restoreCoverage(const uint8_t* alpha,const uint8_t* expect) const {
  if(!alpha||!expect||!alpha_||!expect_)return;
  std::memcpy(alpha_.get(),alpha,PIXELS);
  std::memcpy(expect_.get(),expect,PIXELS);
}
bool HalDisplay::currentAlpha(uint8_t* out) const {
  if(!raster_||!out||!alpha_||!expect_)return false;
  std::vector<uint8_t> ink(BUFFER_SIZE);
  for(unsigned i=0;i<BUFFER_SIZE;i++)ink[i]=static_cast<uint8_t>(~raster_[i]);
  books::Q2Display::computeAlpha(ink.data(),alpha_.get(),expect_.get(),out);
  return true;
}
void HalDisplay::clearScreen(uint8_t color) const {
  zoneCount_=0;  // a new screen: its colours are set again as it is drawn
  if(expect_ && !loaned_)std::memset(expect_.get(),0,PIXELS);  // and its coverage
  if (raster_ && !loaned_) std::memset(raster_.get(), color, BUFFER_SIZE);
}
void HalDisplay::drawImage(const uint8_t* img, uint16_t x, uint16_t y,
                           uint16_t w,uint16_t h,bool) const {
  if (!raster_ || !img || loaned_ || x >= DISPLAY_WIDTH || y >= DISPLAY_HEIGHT) return;
  const unsigned srcStride=(w+7)/8;
  for(unsigned iy=0;iy<h && y+iy<DISPLAY_HEIGHT;++iy) {
    for(unsigned ix=0;ix<w && x+ix<DISPLAY_WIDTH;++ix) {
      const bool white=(img[iy*srcStride+ix/8]>>(7-(ix&7)))&1;
      uint8_t& dest=raster_[(y+iy)*DISPLAY_WIDTH_BYTES+(x+ix)/8];
      uint8_t mask=static_cast<uint8_t>(1u<<(7-((x+ix)&7)));
      if(white) dest|=mask;
      else dest&=static_cast<uint8_t>(~mask);
    }
  }
}
// BOOKS_CORNER_CHECK=1 (tests): report ink under the Q2's rounded glass, by
// Q2 Pod's calibration (80 px radius, no slack), once per presented frame.
static void reportCornerInk(const uint8_t* raster) {
  static const bool on=[]{const char* v=std::getenv("BOOKS_CORNER_CHECK");return v&&*v=='1';}();
  if(!on)return;
  constexpr int R=80;
  int count=0,fx=-1,fy=-1;
  for(int y=0;y<HalDisplay::DISPLAY_HEIGHT;++y) {
    const int d=std::clamp(std::max(R-y,y-(HalDisplay::DISPLAY_HEIGHT-R)),0,R);
    const int hidden=static_cast<int>(std::ceil(R-std::sqrt(double(R*R-d*d))));
    for(int x=0;x<HalDisplay::DISPLAY_WIDTH;++x) {
      if(x>=hidden && x<HalDisplay::DISPLAY_WIDTH-hidden)continue;
      if(raster[y*HalDisplay::DISPLAY_WIDTH_BYTES+x/8]&(0x80u>>(x&7)))continue;  // paper
      if(!count++){fx=x;fy=y;}
    }
  }
  std::fprintf(stderr,"Books corner check: %d ink pixels under the glass%s",count,count?"":"\n");
  if(count)std::fprintf(stderr," (first at %d,%d)\n",fx,fy);
}
void HalDisplay::displayBuffer(RefreshMode,bool) {
  if (!raster_ || !inkRaster_ || loaned_) return;
  reportCornerInk(raster_.get());
  // Upstream CrossPoint framebuffer is 1=paper, 0=ink. Q2PagePresenter's
  // bridge is 1=ink. Invert only the bounded 15KB 1bpp image, not the renderer.
  static books::Q2PagePresenter presenter;
  static bool tried=false, available=false;
  if (!tried) {tried=true; available=presenter.open();}
  if (!available) return;
  for(unsigned i=0;i<BUFFER_SIZE;i++)inkRaster_[i]=static_cast<uint8_t>(~raster_[i]);
  const uint8_t* levels=nullptr;
  if(alpha_ && expect_ && shown_) {
    books::Q2Display::computeAlpha(inkRaster_.get(),alpha_.get(),expect_.get(),shown_.get());
    levels=shown_.get();
  }
  if(themed_) {
    std::vector<books::Q2Display::Zone> zones;
    zones.reserve(static_cast<size_t>(zoneCount_));
    for(int i=0;i<zoneCount_;++i) {
      const auto& z=zones_[i];
      zones.push_back({z.x,z.y,z.w,z.h,z.radius,z.ink,z.paper,z.paperRows,z.rowCount});
    }
    if(!presenter.presentThemed(inkRaster_.get(),BUFFER_SIZE,baseInk_,basePaper_,
                                zones.data(),zones.size(),levels))
      std::fputs("Books: framebuffer publish failed\n",stderr);
    return;
  }
  if(!presenter.presentBW(inkRaster_.get(),BUFFER_SIZE,0,inverted_,levels))
    std::fputs("Books: framebuffer publish failed\n",stderr);
}
uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) {
  if (loaned_ || !raster_)return nullptr;
  loaned_=true;
  if(sizeOut)*sizeOut=BUFFER_SIZE;
  return raster_.get();
}
void HalDisplay::returnFrameBufferStorage() {
  loaned_=false;
  clearScreen();
}
