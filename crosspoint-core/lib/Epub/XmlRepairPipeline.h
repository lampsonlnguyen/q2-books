#pragma once
// Entity repair, then element repair, for a chapter read in chunks. Each
// repaired piece is handed to a callback (the expat feed). Holds its buffers
// itself so the parser and the host sink share one code path.

#include <cstddef>

#include "Epub/XmlEntityRepair.h"
#include "Epub/XmlTagRepair.h"

class XmlRepairPipeline {
 public:
  static constexpr size_t kSlice = 1024;      // input read size
  static constexpr size_t kPieceOut = 2048;   // output room per emitted piece

  // Repairs data[0..len) and calls emit(const char*, size_t) for each output
  // piece. Pass last=true with the final input. emit returns false to stop;
  // push then returns false.
  template <typename Emit>
  bool push(const char* data, size_t len, bool last, Emit&& emit) {
    size_t off = 0;
    do {
      const size_t n = len - off < kSlice ? len - off : kSlice;
      const bool finalSlice = last && off + n == len;
      if (!pushSlice(data + off, n, finalSlice, emit)) return false;
      off += n;
    } while (off < len);
    return true;
  }

 private:
  template <typename Emit>
  bool pushSlice(const char* data, size_t len, bool last, Emit& emit) {
    const size_t stage = entity_.process(data, len, last, entityOut_);
    size_t consumed = 0;
    for (;;) {
      size_t used = 0;
      const size_t n = tags_.feed(entityOut_ + consumed, stage - consumed, last,
                                  pieceOut_, kPieceOut, &used);
      consumed += used;
      if (n && !emit(pieceOut_, n)) return false;
      if (consumed >= stage && tags_.idle(last)) return true;
    }
  }

  XmlEntityRepair entity_;
  XmlTagRepair tags_;
  char entityOut_[XmlEntityRepair::maxOutput(kSlice)];
  char pieceOut_[kPieceOut];
};
