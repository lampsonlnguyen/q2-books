#include "InflateStream.h"
#include <algorithm>
#include <climits>
#include <cstring>

bool InflateStream::init(bool) {
  deinit();
  stream_ = {};
  ready_ = inflateInit2(&stream_, -MAX_WBITS) == Z_OK;
  return ready_;
}
void InflateStream::deinit() {
  if (ready_) inflateEnd(&stream_);
  stream_ = {};
  ready_ = finished_ = failed_ = source_mode_ = false;
  fill_ = nullptr;
  ctx_ = nullptr;
  source_ = nullptr;
  source_left_ = 0;
}
void InflateStream::setSource(const uint8_t* src, size_t len) {
  source_ = src;
  source_left_ = len;
  source_mode_ = true;
  fill_ = nullptr;
}
void InflateStream::setFill(FillFn fn, void* ctx) {
  fill_ = fn;
  ctx_ = ctx;
  source_mode_ = false;
}
bool InflateStream::read(uint8_t* dest, size_t len) {
  size_t total = 0;
  while (total < len) {
    size_t n = 0;
    const Status status = readAtMost(dest + total, len - total, &n);
    total += n;
    if (status == Status::Error || (status == Status::Done && total != len) || n == 0) return false;
  }
  return true;
}
InflateStream::Status InflateStream::readAtMost(uint8_t* dest, size_t maxLen, size_t* produced) {
  if (!produced) return Status::Error;
  *produced = 0;
  if (!ready_ || failed_ || (!dest && maxLen)) return Status::Error;
  if (finished_) return Status::Done;
  if (maxLen == 0) return Status::Ok;

  while (*produced < maxLen) {
    if (!stream_.avail_in) {
      const uint8_t* input = nullptr;
      size_t len = 0;
      if (source_mode_) {
        input = source_;
        len = source_left_;
        source_ += len;
        source_left_ = 0;
      } else if (fill_) {
        len = fill_(ctx_, &input);
      }
      if (!len || !input) { failed_ = true; return Status::Error; }
      stream_.next_in = const_cast<Bytef*>(input);
      stream_.avail_in = static_cast<uInt>(std::min<size_t>(len, UINT_MAX));
    }
    const size_t remaining = maxLen - *produced;
    stream_.next_out = dest + *produced;
    stream_.avail_out = static_cast<uInt>(std::min<size_t>(remaining, UINT_MAX));
    const uInt before = stream_.avail_out;
    const uInt beforeIn = stream_.avail_in;
    const int rc = inflate(&stream_, Z_NO_FLUSH);
    *produced += before - stream_.avail_out;
    if (rc == Z_STREAM_END) { finished_ = true; return Status::Done; }
    if (rc != Z_OK || (before == stream_.avail_out && beforeIn == stream_.avail_in)) {
      failed_ = true;
      return Status::Error;
    }
  }
  return Status::Ok;
}
