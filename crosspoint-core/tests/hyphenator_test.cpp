#include <Hyphenator.h>
#include <cassert>
#include <cstddef>
#include <string>
#include <vector>

int main() {
  const std::string word = "representation";
  Hyphenator::setPreferredLanguage("en");
  auto breaks = Hyphenator::breakOffsets(word, false);
  assert(!breaks.empty());
  for (const auto& br : breaks) {
    assert(br.byteOffset > 0 && br.byteOffset < word.size());
    assert(br.requiresInsertedHyphen);
  }
  // Explicit hyphen boundaries work even in unsupported language tags.
  Hyphenator::setPreferredLanguage("vi");
  auto explicitBreaks = Hyphenator::breakOffsets("word-boundary", false);
  assert(!explicitBreaks.empty());
  Hyphenator::setPreferredLanguage("fr");
  auto french = Hyphenator::breakOffsets("extraordinaire", false);
  for (const auto& br : french)
    assert(br.byteOffset > 0 && br.byteOffset < std::string("extraordinaire").size());
  return 0;
}
