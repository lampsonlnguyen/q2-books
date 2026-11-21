// SPDX-License-Identifier: MIT
#include <MemoryManager.h>
#include <cassert>
#include <cstddef>

int main() {
  auto& mm=freeink::MemoryManager::instance();
  int shelf=0,reader=0;
  mm.setSinkOwner(&shelf);
  mm.registerSink({"shelf-font-cache",50,[](size_t) { return 0u; }});
  mm.setSinkOwner(&reader);
  mm.registerSink({"reader-font-cache",50,[](size_t) { return 0u; }});
  mm.setSinkOwner(nullptr);
  assert(mm.sinkCount()==2);
  // When opening a book, the old shelf tears down after reader.begin().
  // It must not invalidate reader callbacks.
  mm.unregisterSinks(&shelf);
  assert(mm.sinkCount()==1);
  mm.unregisterSinks(&reader);
  assert(mm.sinkCount()==0);
  // Repeated screen transitions must not accumulate stale callbacks.
  for(int n=0;n<100;++n) {
    mm.setSinkOwner(&reader);
    mm.registerSink({"reader-font-cache",50,[](size_t){return 0u;}});
    mm.setSinkOwner(nullptr);
    mm.unregisterSinks(&reader);
    assert(mm.sinkCount()==0);
  }
  return 0;
}
