#pragma once
#include <cstddef>
// CrossPoint's cache eviction hook is optional on Linux. The reader owns
// memory budgets and can register cache reclamation once glyph caching lands.
// WordStore already retries its bounded allocation once and reports OOM.
namespace freeink {
class MemoryManager {
 public:
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  bool ensureFree(std::size_t) { return false; }
};
}
