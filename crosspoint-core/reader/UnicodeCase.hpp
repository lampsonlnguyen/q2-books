// SPDX-License-Identifier: MIT
#pragma once
// Simple (one-to-one) Unicode case mapping for the scripts a dictionary
// lookup meets most: Latin-1, Latin Extended-A/B (Vietnamese ơ ư Đ),
// Latin Extended Additional (Vietnamese ạ ả ấ ...), Greek and Cyrillic.
// Other code points are left as they are. UTF-8 in, UTF-8 out.
#include <cstdint>
#include <string>

namespace books {

inline uint32_t lowerCodepoint(uint32_t c) {
  if (c < 0x80) return (c >= 'A' && c <= 'Z') ? c + 32 : c;
  if (c >= 0xC0 && c <= 0xDE && c != 0xD7) return c + 0x20;
  if (c >= 0x100 && c <= 0x137 && c != 0x130) return c | 1u;  // pairs: even upper, odd lower
  if (c >= 0x139 && c <= 0x148) return (c & 1u) ? c + 1 : c;   // odd upper, even lower
  if (c >= 0x14A && c <= 0x177) return c | 1u;
  if (c == 0x178) return 0xFF;
  if (c >= 0x179 && c <= 0x17E) return (c & 1u) ? c + 1 : c;
  if (c == 0x1A0 || c == 0x1AF) return c + 1;                  // Ơ Ư
  if (c >= 0x1E00 && c <= 0x1EFF && c != 0x1E9E) return c | 1u;  // Latin Extended Additional
  if (c >= 0x391 && c <= 0x3AB && c != 0x3A2) return c + 0x20;  // Greek capitals
  if (c >= 0x410 && c <= 0x42F) return c + 0x20;                // Cyrillic А-Я
  if (c >= 0x400 && c <= 0x40F) return c + 0x50;                // Ѐ-Џ
  return c;
}

inline uint32_t upperCodepoint(uint32_t c) {
  if (c < 0x80) return (c >= 'a' && c <= 'z') ? c - 32 : c;
  if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 0x20;
  if (c == 0xFF) return 0x178;
  if (c >= 0x101 && c <= 0x137 && (c & 1u) && c != 0x131) return c - 1;
  if (c >= 0x13A && c <= 0x148 && !(c & 1u)) return c - 1;
  if (c >= 0x14B && c <= 0x177 && (c & 1u)) return c - 1;
  if (c >= 0x17A && c <= 0x17E && !(c & 1u)) return c - 1;
  if (c == 0x1A1 || c == 0x1B0) return c - 1;
  if (c >= 0x1E01 && c <= 0x1EFF && (c & 1u)) return c - 1;
  if (c >= 0x3B1 && c <= 0x3CB && c != 0x3C2) return c - 0x20;
  if (c >= 0x430 && c <= 0x44F) return c - 0x20;
  if (c >= 0x450 && c <= 0x45F) return c - 0x50;
  return c;
}

namespace utf8case {
// Decode one code point at s[i], advancing i; invalid bytes come back as
// themselves (so the text is never lost).
inline uint32_t next(const std::string& s, size_t& i) {
  const unsigned char b = static_cast<unsigned char>(s[i]);
  const int n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
  if (n == 0 || i + n > s.size()) { ++i; return 0x80000000u | b; }
  uint32_t c = n == 1 ? b : n == 2 ? (b & 0x1Fu) : n == 3 ? (b & 0x0Fu) : (b & 0x07u);
  for (int k = 1; k < n; ++k) {
    const unsigned char t = static_cast<unsigned char>(s[i + k]);
    if ((t & 0xC0) != 0x80) { ++i; return 0x80000000u | b; }
    c = (c << 6) | (t & 0x3Fu);
  }
  i += n;
  return c;
}
inline void put(std::string& out, uint32_t c) {
  if (c & 0x80000000u) { out.push_back(static_cast<char>(c & 0xFF)); return; }
  if (c < 0x80) out.push_back(static_cast<char>(c));
  else if (c < 0x800) { out.push_back(static_cast<char>(0xC0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 0x3F))); }
  else if (c < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (c >> 12)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (c >> 18)));
    out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
  }
}
}  // namespace utf8case

inline std::string utf8Lower(const std::string& s) {
  std::string out;
  out.reserve(s.size());
  for (size_t i = 0; i < s.size();) {
    const uint32_t c = utf8case::next(s, i);
    utf8case::put(out, (c & 0x80000000u) ? c : lowerCodepoint(c));
  }
  return out;
}
// First letter upper, the rest as given ("đà lạt" -> "Đà lạt").
inline std::string utf8CapitalizeFirst(const std::string& s) {
  if (s.empty()) return s;
  size_t i = 0;
  const uint32_t c = utf8case::next(s, i);
  std::string out;
  utf8case::put(out, (c & 0x80000000u) ? c : upperCodepoint(c));
  out.append(s, i, std::string::npos);
  return out;
}
// Every word's first letter upper ("đà lạt" -> "Đà Lạt").
inline std::string utf8TitleCase(const std::string& s) {
  std::string out;
  bool start = true;
  for (size_t i = 0; i < s.size();) {
    const uint32_t c = utf8case::next(s, i);
    utf8case::put(out, (c & 0x80000000u) ? c : start ? upperCodepoint(c) : c);
    start = c == ' ';
  }
  return out;
}

}  // namespace books
