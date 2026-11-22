// SPDX-License-Identifier: MIT
#include "Q2Framebuffer.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>

namespace books {
bool Q2Framebuffer::rotateARGB(const uint32_t* src, size_t pixels,
                               uint8_t* dst, size_t bytes, size_t stride) {
  if (!src || !dst || pixels < LogicalWidth * LogicalHeight ||
      stride < PanelWidth * sizeof(uint32_t) ||
      bytes < (PanelHeight - 1) * stride + PanelWidth * sizeof(uint32_t)) return false;
  for (unsigned ly = 0; ly < LogicalHeight; ++ly) {
    const unsigned px = PanelWidth - 1 - ly;
    for (unsigned lx = 0; lx < LogicalWidth; ++lx) {
      const uint32_t pixel = src[ly * LogicalWidth + lx];
      std::memcpy(dst + lx * stride + px * sizeof(pixel), &pixel, sizeof(pixel));
    }
  }
  return true;
}
bool Q2Framebuffer::open(const char* path) {
  close();
  if (!path) return false;
  const int fd = ::open(path, O_RDWR | O_CLOEXEC);
  if (fd < 0) return false;
  fb_var_screeninfo v{};
  fb_fix_screeninfo f{};
  const bool compatible =
      ::ioctl(fd, FBIOGET_VSCREENINFO, &v) == 0 &&
      ::ioctl(fd, FBIOGET_FSCREENINFO, &f) == 0 &&
      v.xres == PanelWidth && v.yres == PanelHeight &&
      v.yres_virtual >= 2 * PanelHeight && v.bits_per_pixel == 32 &&
      v.red.offset == 16 && v.green.offset == 8 && v.blue.offset == 0 &&
      f.line_length >= PanelWidth * 4 &&
      f.smem_len >= static_cast<uint64_t>(2 * PanelHeight) * f.line_length;
  if (!compatible) { ::close(fd); return false; }
  void* mem = ::mmap(nullptr, f.smem_len, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (mem == MAP_FAILED) { ::close(fd); return false; }
  fd_ = fd;
  mem_ = static_cast<uint8_t*>(mem);
  bytes_ = f.smem_len;
  fix_ = f;
  var_ = v;
  original_yoffset_ = v.yoffset;
  back_yoffset_ = v.yoffset == 0 ? PanelHeight : 0;
  return true;
}
bool Q2Framebuffer::presentARGB(const uint32_t* frame, size_t pixels) {
  if (fd_ < 0) return false;
  const size_t offset = static_cast<size_t>(back_yoffset_) * fix_.line_length;
  if (offset >= bytes_ ||
      !rotateARGB(frame, pixels, mem_ + offset, bytes_ - offset, fix_.line_length))
    return false;
  var_.yoffset = back_yoffset_;
  if (::ioctl(fd_, FBIOPAN_DISPLAY, &var_) != 0) return false;
  back_yoffset_ = back_yoffset_ ? 0 : PanelHeight;
  return true;
}
void Q2Framebuffer::close() {
  if (fd_ >= 0) {
    if (var_.yoffset != original_yoffset_) {
      var_.yoffset = original_yoffset_;
      ::ioctl(fd_, FBIOPAN_DISPLAY, &var_);
    }
    if (mem_) ::munmap(mem_, bytes_);
    ::close(fd_);
  }
  fd_ = -1;
  mem_ = nullptr;
  bytes_ = 0;
}
} // namespace books
