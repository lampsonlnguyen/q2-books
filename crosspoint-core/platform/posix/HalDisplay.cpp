// SPDX-License-Identifier: MIT
#include "HalDisplay.h"
#include "Q2PagePresenter.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <new>
#include <vector>

HalDisplay::HalDisplay() : raster_(new (std::nothrow) uint8_t[BUFFER_SIZE]),
  inkRaster_(new (std::nothrow) uint8_t[BUFFER_SIZE]) {
  clearScreen();
}
HalDisplay::~HalDisplay() = default;
void HalDisplay::begin(bool) { if (!raster_) return; }
void HalDisplay::clearScreen(uint8_t color) const {
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
  static crosspod::Q2PagePresenter presenter;
  static bool tried=false, available=false;
  if (!tried) {tried=true; available=presenter.open();}
  if (!available) return;
  for(unsigned i=0;i<BUFFER_SIZE;i++)inkRaster_[i]=static_cast<uint8_t>(~raster_[i]);
  if(!presenter.presentBW(inkRaster_.get(),BUFFER_SIZE,0,inverted_))
    std::fputs("CrossPod: framebuffer publish failed\n",stderr);
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
