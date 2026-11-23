// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
// Linux/Q2 CrossPoint renderer HAL. A 375x320 1-bpp logical canvas is
// presented through Q2PagePresenter; *no* e-ink or ESP32 hardware API.
class HalDisplay {
 public:
  static constexpr uint16_t DISPLAY_WIDTH=375;
  static constexpr uint16_t DISPLAY_HEIGHT=320;
  static constexpr uint16_t DISPLAY_WIDTH_BYTES=(DISPLAY_WIDTH+7)/8;
  static constexpr uint32_t BUFFER_SIZE=DISPLAY_WIDTH_BYTES*DISPLAY_HEIGHT;
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };
  enum class GrayscaleMode { Overlay, Native };
  enum class GrayscaleBase { White, Black };
  enum class GrayscaleEncoding { Differential, Absolute };
  struct GrayscaleCapabilities {
    bool asyncBase=false;
    bool stripUploads=false;
    bool retainedOverlay=false;
    bool baseFrame=false;
    bool available=false;
  };
  HalDisplay();
  ~HalDisplay();
  void begin(bool seamless=false);
  void clearScreen(uint8_t color=0xFF) const;
  void drawImage(const uint8_t* bytes,uint16_t x,uint16_t y,uint16_t w,uint16_t h,bool fromProgmem=false) const;
  void displayBuffer(RefreshMode mode=FAST_REFRESH,bool turnOffScreen=false);
  void displayBufferAsync(RefreshMode mode=FAST_REFRESH) {displayBuffer(mode);}
  void waitRefreshComplete() {}
  bool supportsAsyncRefresh() const {return false;}
  void setInverted(bool inverted) {inverted_=inverted;}
  bool isInverted() const {return inverted_;}
  void deepSleep() {}
  void controllerIdle() {}
  uint8_t* getFrameBuffer() const {return raster_.get();}
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage();
  void preconditionGrayscale() {}
  void preconditionGrayscale(uint16_t,uint16_t,uint16_t,uint16_t) {}
  bool displayGrayscaleBase(GrayscaleMode,RefreshMode=HALF_REFRESH,bool=false) {return false;}
  void displayGrayscaleBase(RefreshMode=HALF_REFRESH,bool=false) {}
  // Smooth text: the engine's two grayscale passes are kept as planes and
  // blended with the BW page at present time (see Q2Display::computeLevels).
  void copyGrayscaleLsbBuffers(const uint8_t* plane) const {copyPlane(grayLsb_.get(),plane);}
  void copyGrayscaleMsbBuffers(const uint8_t* plane) const {copyPlane(grayMsb_.get(),plane);}
  void cleanupGrayscaleBuffers(const uint8_t*) const {grayValid_=false;}
  // Marks the planes as belonging to this BW page, which the reader puts
  // back in the framebuffer after the grey passes.
  void setGrayPage(const uint8_t* bwPage) const;
  // Restores planes kept with a cached page.
  void restoreGray(const uint8_t* bwPage,const uint8_t* lsb,const uint8_t* msb) const;
  void setGrayInverted(bool inverted) const {grayInverted_=inverted;}
  bool grayValid() const {return grayValid_;}
  const uint8_t* grayLsb() const {return grayLsb_.get();}
  const uint8_t* grayMsb() const {return grayMsb_.get();}
  // Coverage levels (Width*Height bytes, 0..3) of the framebuffer as it would
  // be shown now; false when there are no valid grey planes.
  bool currentLevels(uint8_t* levels) const;
  void displayGrayBuffer(bool=false) {}
  bool displayRetainedGrayscaleOverlay(uint16_t,uint16_t,uint16_t,uint16_t,bool) {return false;}
  void writeGrayscalePlaneStrip(bool,const uint8_t*,uint16_t,uint16_t) {}
  GrayscaleCapabilities grayscaleCapabilities(GrayscaleMode=GrayscaleMode::Overlay) const {return {};}
  uint16_t getDisplayWidth() const {return DISPLAY_WIDTH;}
  uint16_t getDisplayHeight() const {return DISPLAY_HEIGHT;}
  uint16_t getDisplayWidthBytes() const {return DISPLAY_WIDTH_BYTES;}
  uint32_t getBufferSize() const {return BUFFER_SIZE;}
  // Q2 Pod theme colours. The canvas stays 1 bit per pixel; at present time
  // each pixel takes the ink or paper colour of the last zone holding it,
  // else the base palette. A zone may round its corners and give its paper
  // one colour per row (a gradient, as Q2 Pod's Classic bars and bands).
  struct ColorZone {
    int16_t x=0,y=0,w=0,h=0;
    uint8_t radius=0;
    uint32_t ink=0,paper=0;   // 0xRRGGBB
    const uint32_t* paperRows=nullptr;  // optional, rowCount colours top to bottom
    uint8_t rowCount=0;
  };
  static constexpr int kMaxZones=64;
  void setPalette(uint32_t ink,uint32_t paper) {themed_=true;baseInk_=ink;basePaper_=paper;}
  bool themed() const {return themed_;}
  void addZone(const ColorZone& zone) const {if(zoneCount_<kMaxZones)zones_[zoneCount_++]=zone;}
  void clearZones() const {zoneCount_=0;}
  int zoneCount() const {return zoneCount_;}
 private:
  bool themed_=false;
  uint32_t baseInk_=0,basePaper_=0xffffff;
  mutable ColorZone zones_[kMaxZones];
  mutable int zoneCount_=0;
  mutable std::unique_ptr<uint8_t[]> raster_;
  std::unique_ptr<uint8_t[]> inkRaster_;
  std::unique_ptr<uint8_t[]> grayPage_,grayLsb_,grayMsb_,levels_;
  mutable bool grayValid_=false,grayInverted_=false;
  void copyPlane(uint8_t* dest,const uint8_t* src) const {
    if(dest&&src) for(uint32_t i=0;i<BUFFER_SIZE;++i)dest[i]=src[i];
  }
  bool inverted_=false;
  bool loaned_=false;
};
