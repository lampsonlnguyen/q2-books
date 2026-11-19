// SPDX-License-Identifier: MIT
#pragma once
#include <cstddef>
#include <cstdint>
#include <linux/fb.h>
#include <string>

namespace crosspod {
// Shanling Q2: native /dev/fb0 320x375x32bpp, two virtual pages.
// CrossPoint's logical 375x320 pages are rotated into the native LCD.
class Q2Framebuffer {
 public:
  static constexpr unsigned LogicalWidth = 375;
  static constexpr unsigned LogicalHeight = 320;
  static constexpr unsigned PanelWidth = 320;
  static constexpr unsigned PanelHeight = 375;
  Q2Framebuffer() = default;
  ~Q2Framebuffer() { close(); }
  Q2Framebuffer(const Q2Framebuffer&) = delete;
  Q2Framebuffer& operator=(const Q2Framebuffer&) = delete;

  bool open(const char* path = "/dev/fb0");
  bool presentARGB(const uint32_t* frame, size_t pixels);
  void close();
  bool isOpen() const { return fd_ >= 0; }

  // Pure rotation routine: input row-major ARGB 375x320, output pixels
  // x_phys = 319 - y_logical, y_phys = x_logical (Q2 native orientation).
  static bool rotateARGB(const uint32_t* src, size_t pixels,
                         uint8_t* dst, size_t bytes, size_t stride);
 private:
  int fd_ = -1;
  uint8_t* mem_ = nullptr;
  size_t bytes_ = 0;
  fb_var_screeninfo var_{};
  fb_fix_screeninfo fix_{};
  unsigned original_yoffset_ = 0;
  unsigned back_yoffset_ = PanelHeight;
};
} // namespace crosspod
