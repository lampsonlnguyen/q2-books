// Menus follow Q2 Pod's language: English by default; config.ini's
// [Q2POD] LANG only while Q2 Pod's copy of the language is there; Books'
// table wins over shared firmware labels; mismatched printf conversions skipped;
// letters the menu font lacks shown in English.
// Also checks Books' shipped tables (lang/*.tsv) against the template.
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>

#include "UiLanguage.hpp"

static void put(const std::string& path, const std::string& text) {
  FILE* f = std::fopen(path.c_str(), "wb");
  assert(f && std::fputs(text.c_str(), f) >= 0 && std::fclose(f) == 0);
}
static bool validUtf8(const std::string& s) {
  for (size_t i = 0; i < s.size();) {
    const unsigned char b = static_cast<unsigned char>(s[i]);
    const size_t n = b < 0x80 ? 1 : (b >> 5) == 6 ? 2 : (b >> 4) == 14 ? 3 : (b >> 3) == 30 ? 4 : 0;
    if (!n || i + n > s.size()) return false;
    for (size_t k = 1; k < n; ++k)
      if ((static_cast<unsigned char>(s[i + k]) & 0xC0) != 0x80) return false;
    i += n;
  }
  return true;
}

int main() {
  char dir[] = "/tmp/books-lang-XXXXXX";
  assert(mkdtemp(dir));
  const std::string root = dir, data = root + "/data", state = root + "/books";
  assert(system(("mkdir -p " + data + " " + state + "/lang").c_str()) == 0);
  setenv("BOOKS_STATE_DIR", state.c_str(), 1);
  setenv("BOOKS_SYSTEM_CONFIG", (data + "/config.ini").c_str(), 1);
  unsetenv("BOOKS_LANG");

  // No config.ini: English, and tr() hands back the same pointer.
  books::reloadUiLanguage();
  const char* english = "Reading settings";
  assert(!books::uiTranslated() && books::tr(english) == english);

  // LANG=de with no copy of the language (the pack removed): English.
  put(data + "/config.ini", "[SYSSET]\nLANGUAGE=2\n[Q2POD]\nWHEELSENSITIVITY=50\nLANG = de \n");
  books::reloadUiLanguage();
  assert(!books::uiTranslated());

  // With Q2 Pod's copy: Books' labels take precedence over shared labels.
  put(data + "/q2-lang-de.tsv",
      "# Deutsch\nmsg_no\tCancel\tAbbrechen\nSave\tSave\tSpeichern\nSearch\tSearch\tSuche\n"
      "%u songs\t%u songs\t%s Titel\n");  // wrong conversion: skipped
  put(state + "/lang/de.tsv",
      "# Deutsch\nReading settings\tReading settings\tLese\\neinstellungen\n"
      "Search\tSearch\tSuchen (Books)\nBooks: %zu/%zu\tBooks: %zu/%zu\tBücher: %zu/%zu\n"
      "Online\tOnline\t\n"                         // no translation: English
      "Bad\tBooks: %zu/%zu\tBücher %s\n"            // conversions differ: skipped
      "Wide\tWide\t\xE5\xBA\x83\n");               // a CJK letter
  books::reloadUiLanguage();
  assert(books::uiTranslated() && books::uiLanguageCode() == "de");
  assert(books::tr(std::string("Reading settings")) == "Lese\neinstellungen");
  assert(books::tr(std::string("Cancel")) == "Cancel");  // firmware-specific key
  assert(books::tr(std::string("Save")) == "Speichern");  // shared firmware label
  assert(books::tr(std::string("Search")) == "Suchen (Books)");
  assert(books::tr(std::string("Books: %zu/%zu")) == "Bücher: %zu/%zu");
  assert(books::tr(std::string("Online")) == "Online" && books::tr(std::string("%u songs")) == "%u songs");
  assert(books::tr(std::string("A book title")) == "A book title");
  // A menu font with Latin only: the CJK translation stays English.
  assert(books::tr(std::string("Wide")) == "\xE5\xBA\x83");
  books::setUiGlyphCheck([](uint32_t cp) { return cp < 0x3000; });
  assert(books::tr(std::string("Wide")) == "Wide" && books::tr(std::string("Search")) == "Suchen (Books)");

  // A new firmware language with no Books table can still supply shared labels.
  put(data + "/q2-lang-id.tsv", "# Bahasa Indonesia\nSearch\tSearch\tCari\n");
  put(data + "/config.ini", "[Q2POD]\nLANG=id\n");
  books::reloadUiLanguage();
  assert(books::uiLanguageCode() == "id" && books::tr(std::string("Search")) == "Cari");
  assert(books::tr(std::string("Reading settings")) == "Reading settings");

  // Switch the firmware to Vietnamese, using the actual shipped Books table.
  setenv("BOOKS_STATE_DIR", BOOKS_LANG_DIR "/..", 1);
  put(data + "/q2-lang-vi.tsv", "# Tiếng Việt\nSave\tSave\tLưu\n");
  put(data + "/config.ini", "[Q2POD]\nLANG=vi\n");
  books::reloadUiLanguage();
  assert(books::uiLanguageCode() == "vi" && books::tr(std::string("Books")) == "Sách");
  assert(books::tr(std::string("Cannot save EPUB")) == "Không thể lưu EPUB");
  setenv("BOOKS_LANG", "hi_IN", 1);
  books::reloadUiLanguage();
  assert(books::uiLanguageCode() == "hi_IN" && books::tr(std::string("Books")) == "पुस्तकें");
  unsetenv("BOOKS_LANG");
  assert(unlink((data + "/q2-lang-vi.tsv").c_str()) == 0);
  books::reloadUiLanguage();
  assert(!books::uiTranslated() && books::tr(english) == english);
  setenv("BOOKS_STATE_DIR", state.c_str(), 1);

  // Not a code, or another section's LANG: English.
  put(data + "/config.ini", "[Q2POD]\nLANG=../x\n");
  books::reloadUiLanguage();
  assert(!books::uiTranslated());
  put(data + "/config.ini", "[OTHER]\nLANG=de\n");
  books::reloadUiLanguage();
  assert(!books::uiTranslated());
  // BOOKS_LANG (host and tests) picks the language without Q2 Pod.
  setenv("BOOKS_LANG", "de", 1);
  books::reloadUiLanguage();
  assert(books::uiLanguageCode() == "de" && books::tr(std::string("Search")) == "Suchen (Books)");
  books::setUiEnglishFallback(true);
  assert(books::readSystemLanguage() == "de");
  assert(!books::uiTranslated() && books::tr(std::string("Search")) == "Search");
  books::reloadUiLanguage();
  assert(!books::uiTranslated());
  books::setUiEnglishFallback(false);
  assert(books::uiLanguageCode() == "de" && books::tr(std::string("Search")) == "Suchen (Books)");
  unsetenv("BOOKS_LANG");
  assert(books::printfConversions("%zu / %zu 100%% %s") == "%zu%zu%s");

  // The tables Books ships: every line's English is in the template, no
  // English twice, valid UTF-8, the same conversions.
  std::set<std::string> templ;
  {
    std::ifstream in(BOOKS_LANG_DIR "/en.tsv");
    std::string line;
    assert(std::getline(in, line) && line.rfind("# ", 0) == 0);
    while (std::getline(in, line)) {
      const auto t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
      assert(t1 != std::string::npos && t2 != std::string::npos);
      assert(templ.insert(line.substr(t1 + 1, t2 - t1 - 1)).second);
    }
  }
  assert(templ.count("Reading settings") && templ.count("Catalog servers") && templ.count("Books: %zu/%zu"));
  for (const char* code : {"de", "es", "fr", "hi", "it", "ja", "ko", "pl", "ru", "th", "uk", "vi", "zh_CN", "zh_HK"}) {
    std::ifstream in(std::string(BOOKS_LANG_DIR "/") + code + ".tsv");
    std::string line;
    assert(std::getline(in, line) && line.rfind("# ", 0) == 0);
    std::set<std::string> seen;
    while (std::getline(in, line)) {
      const auto t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
      assert(t1 != std::string::npos && t2 != std::string::npos);
      const std::string en = line.substr(t1 + 1, t2 - t1 - 1), tx = line.substr(t2 + 1);
      assert(templ.count(en) && seen.insert(en).second && validUtf8(tx) && !tx.empty());
      assert(books::printfConversions(books::unescapeLanguageField(en)) ==
             books::printfConversions(books::unescapeLanguageField(tx)));
    }
  }
  assert(system(("rm -rf " + root).c_str()) == 0);
  std::puts("Books UI language follows Q2 Pod: PASS");
  return 0;
}
