// M3 translated menus: default English, the saved choice, lookups, and the
// table itself (no duplicate keys, valid UTF-8, no empty text).
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <unistd.h>

#include "UiLanguage.hpp"

static bool validUtf8(const char* s) {
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p;) {
    const int n = *p < 0x80 ? 1 : (*p >> 5) == 6 ? 2 : (*p >> 4) == 14 ? 3 : (*p >> 3) == 30 ? 4 : 0;
    if (!n) return false;
    for (int k = 1; k < n; ++k)
      if ((p[k] & 0xC0) != 0x80) return false;
    p += n;
  }
  return true;
}

int main() {
  char dir[] = "/tmp/books-lang-XXXXXX";
  assert(mkdtemp(dir));
  setenv("BOOKS_STATE_DIR", dir, 1);
  assert(books::uiLanguage() == books::UiLanguage::English);
  assert(std::strcmp(books::tr("Reading settings"), "Reading settings") == 0);
  assert(books::setUiLanguage(books::UiLanguage::Vietnamese));
  assert(std::strcmp(books::tr("Reading settings"), "Cài đặt đọc") == 0);
  assert(std::strcmp(books::tr("A book title"), "A book title") == 0);  // not UI text: unchanged
  assert(books::tr(std::string("On")) == "Bật");
  std::set<std::string> keys;
  for (const auto& t : books::kVietnamese) {
    assert(*t.en && *t.vi && validUtf8(t.vi));
    assert(keys.insert(t.en).second);  // one translation per string
  }
  // Saved: a new process (static re-read) would see Vietnamese.
  FILE* f = std::fopen(books::uiLanguageFile().c_str(), "rb");
  assert(f);
  char line[32] = {};
  assert(std::fgets(line, sizeof line, f) && std::string(line) == "BOOKS-LANG1 vi\n");
  std::fclose(f);
  assert(books::setUiLanguage(books::UiLanguage::English));
  assert(std::strcmp(books::tr("On"), "On") == 0);
  std::system((std::string("rm -rf ") + dir).c_str());
  std::puts("Books UI language: PASS");
  return 0;
}
