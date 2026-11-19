#pragma once
// Transitional test adapter: mirror only the spine callback used by the
// original CrossPoint ContentOpfParser. The full on-disk cache still needs port.
#include <string>
#include <vector>
class BookMetadataCache {
 public:
  std::vector<std::string> spine;
  void createSpineEntry(const std::string& href) { spine.push_back(href); }
};
