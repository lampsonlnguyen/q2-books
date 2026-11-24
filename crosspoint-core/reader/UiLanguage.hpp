// SPDX-License-Identifier: MIT
#pragma once
// Menus in Q2 Pod's own language. Books has no language setting: it reads
// the one Q2 Pod uses, as Q2 Pod's patch/language.c decides it. The code
// is LANG under [Q2POD] in /mnt/data/config.ini, and it counts only while
// Q2 Pod's copy of that language, /mnt/data/q2-lang-<code>.tsv, is there.
// Anything else (no key, a reset config, the pack removed) is English.
//
// Books' table (.books/lang/<code>.tsv) takes precedence over generic
// labels from the active firmware pack. The pack's stock identifier rows
// are specific to firmware screens and are skipped. The table format is a
// first line "# <name>", then key<TAB>English<TAB>translation, with \n, \t
// and \\ escaped. As in Q2 Pod, a line counts only for the English it was
// written for, and only when its printf conversions match the English's.
//
// A translation whose letters the menu font can't draw (setUiGlyphCheck)
// is shown in English, never as boxes or question marks.
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unordered_map>
#include <unistd.h>

namespace books {

inline std::string uiStateDir() {
  const char* env = std::getenv("BOOKS_STATE_DIR");
  return env && *env ? env : "/mnt/mmc/.books";
}
// Q2 Pod's settings file (BOOKS_SYSTEM_CONFIG for tests); its language
// copies sit beside it.
inline std::string systemConfigFile() {
  const char* env = std::getenv("BOOKS_SYSTEM_CONFIG");
  return env && *env ? env : "/mnt/data/config.ini";
}
inline std::string firmwareLanguageTable(const std::string& config, const std::string& code) {
  const auto slash = config.find_last_of('/');
  const std::string dir = slash == std::string::npos ? "." : config.substr(0, slash);
  return dir + "/q2-lang-" + code + ".tsv";
}
// Q2 Pod's codes: letters and '_', 2 to 11 of them (de, zh_CN...).
inline bool validLanguageCode(const std::string& code) {
  if (code.size() < 2 || code.size() > 11) return false;
  for (unsigned char c : code)
    if (!std::isalpha(c) && c != '_') return false;
  return true;
}
// The value of |key| under [section] in an ini file, as Q2 Pod's
// toolsReadConfig reads it (trimmed); empty when absent.
inline std::string readIniValue(const std::string& file, const char* section, const char* key) {
  FILE* f = std::fopen(file.c_str(), "rb");
  if (!f) return {};
  char line[512];
  bool inSection = false;
  std::string found;
  const auto trim = [](std::string s) {
    while (!s.empty() && std::strchr(" \t\r\n", s.back())) s.pop_back();
    size_t i = 0;
    while (i < s.size() && std::strchr(" \t", s[i])) ++i;
    return s.substr(i);
  };
  for (int n = 0; n < 2000 && std::fgets(line, sizeof line, f); ++n) {
    const std::string text = trim(line);
    if (!text.empty() && text.front() == '[') {
      inSection = text == std::string("[") + section + "]";
      continue;
    }
    const auto eq = text.find('=');
    if (inSection && eq != std::string::npos && trim(text.substr(0, eq)) == key) {
      found = trim(text.substr(eq + 1));
      break;
    }
  }
  std::fclose(f);
  return found;
}
// The language Q2 Pod shows ("" for English). BOOKS_LANG overrides it for
// tests and the host.
inline std::string readSystemLanguage() {
  if (const char* env = std::getenv("BOOKS_LANG")) return validLanguageCode(env) ? env : "";
  const std::string config = systemConfigFile();
  const std::string code = readIniValue(config, "Q2POD", "LANG");
  if (!validLanguageCode(code)) return {};
  const std::string copy = firmwareLanguageTable(config, code);
  return ::access(copy.c_str(), R_OK) == 0 ? code : std::string();
}

// The printf conversions of a text, in order ("%zu%s"); "%%" is none.
inline std::string printfConversions(const std::string& text) {
  std::string out;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] != '%') continue;
    if (i + 1 < text.size() && text[i + 1] == '%') { ++i; continue; }
    size_t j = i + 1;
    while (j < text.size() && std::strchr("-+ #0123456789.*hlLzjt", text[j])) ++j;
    if (j < text.size() && std::strchr("diouxXeEfFgGaAcspn", text[j])) {
      out += text.substr(i, j - i + 1);
      i = j;
    }
  }
  return out;
}
inline std::string unescapeLanguageField(const std::string& field) {
  std::string out;
  for (size_t i = 0; i < field.size(); ++i) {
    if (field[i] == '\\' && i + 1 < field.size()) {
      const char e = field[++i];
      out += e == 'n' ? '\n' : e == 't' ? '\t' : e;
    } else out += field[i];
  }
  return out;
}

struct UiTranslation {
  std::string text;
  signed char drawable = -1;  // unknown until first asked
};
struct UiLanguageState {
  bool loaded = false;
  bool englishFallback = false;
  std::string code;
  std::unordered_map<std::string, UiTranslation> table;  // English -> translation
  bool (*glyphCheck)(uint32_t) = nullptr;
};
inline UiLanguageState& uiLanguageState() {
  static UiLanguageState state;
  return state;
}
// Adds a language-pack table's lines to |table| (lines already there win).
// Returns how many it added. At most 2 MB is read.
inline size_t loadLanguageTable(const std::string& file, std::unordered_map<std::string, UiTranslation>& table,
                                bool sharedLabelsOnly = false) {
  struct stat info {};
  if (::stat(file.c_str(), &info) != 0 || !S_ISREG(info.st_mode) || info.st_size > 2 * 1024 * 1024) return 0;
  FILE* f = std::fopen(file.c_str(), "rb");
  if (!f) return 0;
  std::string data(static_cast<size_t>(info.st_size), '\0');
  data.resize(std::fread(data.data(), 1, data.size(), f));
  std::fclose(f);
  size_t added = 0;
  for (size_t start = 0; start < data.size();) {
    size_t end = data.find('\n', start);
    if (end == std::string::npos) end = data.size();
    std::string line = data.substr(start, end - start);
    start = end + 1;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty() || line[0] == '#') continue;
    const auto tab1 = line.find('\t');
    const auto tab2 = tab1 == std::string::npos ? std::string::npos : line.find('\t', tab1 + 1);
    if (tab2 == std::string::npos) continue;
    const std::string english = unescapeLanguageField(line.substr(tab1 + 1, tab2 - tab1 - 1));
    if (sharedLabelsOnly && unescapeLanguageField(line.substr(0, tab1)) != english) continue;
    const std::string translation = unescapeLanguageField(line.substr(tab2 + 1));
    if (english.empty() || translation.empty() || tab1 == 0) continue;
    if (printfConversions(english) != printfConversions(translation)) continue;  // would read the wrong argument
    if (table.emplace(english, UiTranslation{translation, -1}).second) ++added;
  }
  return added;
}
inline void loadUiLanguage(UiLanguageState& s) {
  s.loaded = true;
  s.table.clear();
  s.code = s.englishFallback ? std::string() : readSystemLanguage();
  if (s.code.empty()) return;
  const auto directory = uiStateDir() + "/lang/";
  loadLanguageTable(directory + s.code + ".tsv", s.table);
  if (const auto region = s.code.find('_'); region != std::string::npos && region >= 2)
    loadLanguageTable(directory + s.code.substr(0, region) + ".tsv", s.table);
  if (!std::getenv("BOOKS_LANG"))
    loadLanguageTable(firmwareLanguageTable(systemConfigFile(), s.code), s.table, true);
}
// The code of the language the menus are in ("" for English).
inline const std::string& uiLanguageCode() {
  auto& s = uiLanguageState();
  if (!s.loaded) loadUiLanguage(s);
  return s.code;
}
// True when menus are drawn in another language than English (with the
// reading font's glyphs rather than the pixel font).
inline bool uiTranslated() { return !uiLanguageCode().empty(); }
// Read Q2 Pod's language again (tests; Books itself reads it once a run, as
// Q2 Pod restarts to change it).
inline void reloadUiLanguage() { loadUiLanguage(uiLanguageState()); }
inline void setUiEnglishFallback(bool enabled) {
  auto& s = uiLanguageState();
  if (s.englishFallback == enabled) return;
  s.englishFallback = enabled;
  s.loaded = false;
}
inline void setUiGlyphCheck(bool (*check)(uint32_t)) {
  auto& s = uiLanguageState();
  s.glyphCheck = check;
  for (auto& [english, t] : s.table) t.drawable = -1;
}
inline bool uiTextDrawable(const std::string& text, bool (*check)(uint32_t)) {
  if (!check) return true;
  for (size_t i = 0; i < text.size();) {
    const unsigned char b = static_cast<unsigned char>(text[i]);
    uint32_t cp = b;
    size_t n = 1;
    if (b >= 0xF0) cp = b & 7, n = 4;
    else if (b >= 0xE0) cp = b & 15, n = 3;
    else if (b >= 0xC0) cp = b & 31, n = 2;
    if (i + n > text.size()) return false;
    for (size_t k = 1; k < n; ++k) cp = (cp << 6) | (static_cast<unsigned char>(text[i + k]) & 63);
    i += n;
    if (cp >= 0x20 && !check(cp)) return false;
  }
  return true;
}

inline const char* tr(const char* english) {
  if (!english || !uiTranslated()) return english;
  auto& s = uiLanguageState();
  const auto it = s.table.find(english);
  if (it == s.table.end()) return english;
  if (it->second.drawable < 0) it->second.drawable = uiTextDrawable(it->second.text, s.glyphCheck) ? 1 : 0;
  return it->second.drawable ? it->second.text.c_str() : english;
}
inline std::string tr(const std::string& english) { return tr(english.c_str()); }

}  // namespace books
