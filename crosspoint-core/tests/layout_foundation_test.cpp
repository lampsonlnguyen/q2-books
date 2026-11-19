#include <WordStore.h>
#include <CssParser.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
int main() {
  WordStore store;
  WordStore::StoredWord w{}, next{}, third{};
  const char* english = "Chapter";
  const char* vietnamese = "Nguyễn";
  const char* arabic = "العربية";
  assert(store.append(english, std::strlen(english), w));
  assert(store.append(vietnamese, std::strlen(vietnamese), next));
  assert(store.append(arabic, std::strlen(arabic), third));
  assert(store.view(w) == english);
  assert(store.view(next) == vietnamese);
  assert(store.view(third) == arabic);
  auto tail = WordStore::suffix(w, 3);
  assert(store.view(tail) == "pter");
  store.release(next);
  store.release(third);
  store.release(w);

  // Original parser: test declaration, CSS selectors and source order.
  CssParser parser("");
  auto inlineStyle = CssParser::parseInlineStyle("text-align: center; font-style: italic; font-weight: bold;");
  assert(inlineStyle.textAlign == CssTextAlign::Center);
  assert(inlineStyle.fontStyle == CssFontStyle::Italic);
  assert(inlineStyle.fontWeight == CssFontWeight::Bold);
  HalFile stylesheet(std::tmpfile());
  assert(stylesheet.isOpen());
  const char* rules =
      "p { text-align: left; } p.highlight { text-align: center; font-style: italic; }";
  assert(stylesheet.write(reinterpret_cast<const uint8_t*>(rules),
                          std::strlen(rules)) == std::strlen(rules));
  assert(stylesheet.seek(0));
  assert(parser.loadFromStream(stylesheet) == CssParser::ParseResult::Complete);
  assert(parser.ruleCount() >= 2);
  const auto selected = parser.resolveStyle("p", "highlight");
  assert(selected.textAlign == CssTextAlign::Center);
  assert(selected.fontStyle == CssFontStyle::Italic);
  return 0;
}
