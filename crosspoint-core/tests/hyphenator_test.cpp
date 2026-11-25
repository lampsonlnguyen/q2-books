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
  assert(Hyphenator::rulesFor(" por-BR ") == Hyphenator::rulesFor("pt"));
  assert(Hyphenator::rulesFor("POL") == Hyphenator::rulesFor("pl"));
  assert(Hyphenator::rulesFor("ja") == nullptr);
  const auto inlineEnglish = Hyphenator::rulesFor("ENG-us");
  assert(inlineEnglish && inlineEnglish != Hyphenator::documentRules());
  assert(!Hyphenator::breakOffsets(word, false, inlineEnglish).empty());
  assert(Hyphenator::breakOffsets(word, false, nullptr).empty());
  assert(Hyphenator::documentRules() == Hyphenator::rulesFor("fr-FR"));
  assert(Hyphenator::rulesFor("en-") == nullptr);
  assert(Hyphenator::rulesFor("en--US") == nullptr);
  assert(Hyphenator::rulesFor("EN_us") == Hyphenator::rulesFor("en"));
  assert(Hyphenator::rulesFor("mul") == nullptr);
  for (const auto& br : Hyphenator::breakOffsets("representation", true, nullptr))
    assert(!br.requiresInsertedHyphen);
  for (const auto& br : Hyphenator::breakOffsets("日本語文字列", true, nullptr))
    assert(!br.requiresInsertedHyphen);
  const auto* ukrainian = Hyphenator::rulesFor("uk-UA");
  assert(ukrainian);
  const auto upper = Hyphenator::breakOffsets("УКРАЇНСЬКИЙ", false, ukrainian);
  const auto lower = Hyphenator::breakOffsets("український", false, ukrainian);
  assert(upper.size() == lower.size());
  for (size_t i = 0; i < upper.size(); ++i)
    assert(upper[i].byteOffset == lower[i].byteOffset);
  return 0;
}
