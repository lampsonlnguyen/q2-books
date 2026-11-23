// SPDX-License-Identifier: MIT
#include "HalDisplay.h"
#include "Q2PagePresenter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

HalDisplay::HalDisplay() : raster_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  inkRaster_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  grayPage_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  grayLsb_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  grayMsb_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  levels_(new (std::nothrow) uint8_t[DISPLAY_WIDTH*DISPLAY_HEIGHT]) {
  clearScreen();
}
void HalDisplay::setGrayPage(const uint8_t* bwPage) const {
  if(!bwPage||!grayPage_||!grayLsb_||!grayMsb_){grayValid_=false;return;}
  // Kept as 1 = ink, as the presenter's image.
  for(unsigned i=0;i<BUFFER_SIZE;i++)grayPage_[i]=static_cast<uint8_t>(~bwPage[i]);
  grayValid_=true;
  grayInverted_=false;
}
void HalDisplay::restoreGray(const uint8_t* bwPage,const uint8_t* lsb,const uint8_t* msb) const {
  if(!lsb||!msb){grayValid_=false;return;}
  copyPlane(grayLsb_.get(),lsb);
  copyPlane(grayMsb_.get(),msb);
  setGrayPage(bwPage);
}
bool HalDisplay::currentLevels(uint8_t* levels) const {
  if(!grayValid_||!raster_||!levels)return false;
  std::vector<uint8_t> ink(BUFFER_SIZE);
  for(unsigned i=0;i<BUFFER_SIZE;i++)ink[i]=static_cast<uint8_t>(~raster_[i]);
  books::Q2Display::computeLevels(ink.data(),grayPage_.get(),grayLsb_.get(),grayMsb_.get(),
                                  grayInverted_,levels);
  return true;
}
HalDisplay::~HalDisplay() = default;
void HalDisplay::begin(bool) { if (!raster_) return; }
void HalDisplay::clearScreen(uint8_t color) const {
  zoneCount_=0;  // a new screen: its colours are set again as it is drawn
  grayValid_=false;  // and its grey planes belong to the page drawn before
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
void HalDisplay::displayBuffer(RefreshMode,bool) {
  if (!raster_ || !inkRaster_ || loaned_) return;
  // Upstream CrossPoint framebuffer is 1=paper, 0=ink. Q2PagePresenter's
  // bridge is 1=ink. Invert only the bounded 15KB 1bpp image, not the renderer.
  static books::Q2PagePresenter presenter;
  static bool tried=false, available=false;
  if (!tried) {tried=true; available=presenter.open();}
  if (!available) return;
  for(unsigned i=0;i<BUFFER_SIZE;i++)inkRaster_[i]=static_cast<uint8_t>(~raster_[i]);
  const uint8_t* levels=nullptr;
  if(grayValid_ && levels_) {
    books::Q2Display::computeLevels(inkRaster_.get(),grayPage_.get(),grayLsb_.get(),grayMsb_.get(),
                                    grayInverted_,levels_.get());
    levels=levels_.get();
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
