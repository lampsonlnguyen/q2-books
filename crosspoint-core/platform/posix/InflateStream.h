#pragma once
// Linux replacement for CrossPoint's ESP32 miniz InflateStream interface.
// The original ZipFile.cpp uses this API unchanged; on Q2, zlib handles raw DEFLATE.
#include <cstddef>
#include <cstdint>
#include <zlib.h>

class InflateStream {
 public:
  enum class Status { Ok, Done, Error };
  using FillFn = size_t (*)(void*, const uint8_t**);
  InflateStream() = default;
  ~InflateStream() { deinit(); }
  InflateStream(const InflateStream&) = delete;
  InflateStream& operator=(const InflateStream&) = delete;
  bool init(bool streaming);
  void deinit();
  void setSource(const uint8_t* src, size_t len);
  void setFill(FillFn fn, void* ctx);
  void setZlibWrapped() { /* ZipFile uses raw DEFLATE, not zlib wrapped. */ }
  bool read(uint8_t* dest, size_t len);
  Status readAtMost(uint8_t* dest, size_t maxLen, size_t* produced);
 private:
  z_stream stream_{};
  bool ready_ = false;
  bool finished_ = false;
  bool failed_ = false;
  FillFn fill_ = nullptr;
  void* ctx_ = nullptr;
  const uint8_t* source_ = nullptr;
  size_t source_left_ = 0;
  bool source_mode_ = false;
};
