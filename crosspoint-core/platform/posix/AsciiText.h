#pragma once
#include <string_view>
// Same ASCII-only whitespace semantics used by CrossPoint's tokenizer.
// No Unicode is folded: Unicode codepoint processing stays in Utf8.cpp.
constexpr bool isAsciiWhitespace(char c) {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' ||
         c == '\f' || c == '\v';
}
constexpr std::string_view trimAsciiWhitespace(std::string_view s) {
  while (!s.empty() && isAsciiWhitespace(s.front())) s.remove_prefix(1);
  while (!s.empty() && isAsciiWhitespace(s.back())) s.remove_suffix(1);
  return s;
}
