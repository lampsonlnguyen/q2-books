// SPDX-License-Identifier: MIT
#pragma once
#include "Q2Display.hpp"
#include "Q2Framebuffer.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>

namespace crosspod {
// A bounded ~480 KiB scratch buffer bridges CrossPoint's grayscale/monochrome
// rendering output to the native 320x375 rotated Q2 framebuffer. Page writes
// are explicit: no render thread or per-frame timer is required.
class Q2PagePresenter {
 public:
  bool open(const char* framebuffer = "/dev/fb0") {
    if (!pixels_) {
      pixels_.reset(new (std::nothrow) uint32_t[Q2Display::Width * Q2Display::Height]);
    }
    return pixels_ && fb_.open(framebuffer);
  }

  bool presentBW(const uint8_t* mono, size_t bytes,
                 unsigned warmth = 0, bool night = false) {
    if (!fb_.isOpen() || !pixels_ ||
        !Q2Display::presentBW(mono, bytes, pixels_.get(),
            Q2Display::Width * Q2Display::Height, warmth, night))
      return false;
    return fb_.presentARGB(pixels_.get(), Q2Display::Width * Q2Display::Height);
  }

  bool presentARGB(const uint32_t* rgba, size_t pixels) {
    return fb_.isOpen() && fb_.presentARGB(rgba, pixels);
  }

  void close() { fb_.close(); pixels_.reset(); }
  ~Q2PagePresenter() { close(); }
  Q2PagePresenter() = default;
  Q2PagePresenter(const Q2PagePresenter&) = delete;
  Q2PagePresenter& operator=(const Q2PagePresenter&) = delete;
 private:
  Q2Framebuffer fb_;
  std::unique_ptr<uint32_t[]> pixels_;
};
} // namespace crosspod
