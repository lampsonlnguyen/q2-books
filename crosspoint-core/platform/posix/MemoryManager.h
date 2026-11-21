#pragma once
// POSIX memory-availability and cache reclamation bridge for CrossPoint.
// The Q2 has 128MB shared RAM; checking actual MemAvailable is more useful
// than retaining the ESP32 heap-specific API.
#include <cstddef>
#include <algorithm>
#include <cstdio>
#include <functional>
#include <string>
#include <sys/sysinfo.h>
#include <utility>
#include <vector>
namespace freeink {
class MemoryManager {
 public:
  struct Sink {
    const char* name;
    int priority;
    std::function<std::size_t(std::size_t)> reclaim;
  };
  static MemoryManager& instance() {
    static MemoryManager manager;
    return manager;
  }
  // The shelf and reader renderers briefly overlap during an EPUB open.
  // Keep callbacks attributed to the specific owning renderer, not a global
  // renderer generation: tearing down the old shelf must not remove the
  // newly opened reader's font-cache reclamation handlers.
  void setSinkOwner(const void* owner) { pendingOwner_=owner; }
  void registerSink(Sink sink) {
    sinks_.push_back({std::move(sink),pendingOwner_});
  }
  void unregisterSinks(const void* owner) {
    sinks_.erase(std::remove_if(sinks_.begin(),sinks_.end(),
        [owner](const Registration& r){ return r.owner==owner; }),
        sinks_.end());
    if(pendingOwner_==owner)pendingOwner_=nullptr;
  }
  std::size_t sinkCount() const { return sinks_.size(); }
  std::size_t freeBytes() const {
    // Linux MemAvailable includes reclaimable page cache. This is intentionally
    // not the same as the process RSS cap, which is measured separately.
    std::FILE* fp=std::fopen("/proc/meminfo","r");
    if (!fp) return 0;
    char name[64]{};
    unsigned long kib=0;
    char unit[16]{};
    while (std::fscanf(fp,"%63s %lu %15s",name,&kib,unit)==3) {
      if (std::string(name)=="MemAvailable:") {
        std::fclose(fp);
        return static_cast<std::size_t>(kib)*1024u;
      }
    }
    std::fclose(fp);
    return 0;
  }
  bool ensureFree(std::size_t required) {
    if (freeBytes()>=required) return true;
    // The active renderer owns these callbacks and clears them on teardown.
    for (auto& entry:sinks_) {
      if (entry.sink.reclaim) entry.sink.reclaim(required);
      if (freeBytes()>=required) return true;
    }
    return false;
  }
 private:
  struct Registration {
    Sink sink;
    const void* owner=nullptr;
  };
  const void* pendingOwner_=nullptr;
  std::vector<Registration> sinks_;
};
}
