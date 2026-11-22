// M3: lookups across spellings (Unicode case), phrase fallback, dictionaries
// turned off, and the vocabulary log.
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "OfflineDictionary.hpp"

int main(int argc, char** argv) {
  assert(argc == 2);
  // Case mapping.
  assert(books::utf8Lower("ĐÀ LẠT Ước ΑΒΓ ДОМ Éé") == "đà lạt ước αβγ дом éé");
  assert(books::utf8CapitalizeFirst("đà lạt") == "Đà lạt");
  assert(books::utf8TitleCase("đà lạt") == "Đà Lạt");
  assert(books::utf8Lower("中文 abc") == "中文 abc");
  assert(books::utf8Lower(std::string("\xff\xfe", 2)) == std::string("\xff\xfe", 2));  // bad bytes kept

  char dir[] = "/tmp/books-dict-XXXXXX";
  assert(mkdtemp(dir));
  setenv("BOOKS_STATE_DIR", dir, 1);
  const std::string state = dir;
  assert(std::system(("mkdir -p " + state + "/dictionaries && cp -r " + argv[1] + "/plain " + state +
                      "/dictionaries/").c_str()) == 0);
  std::ofstream(state + "/dictionary.tsv") << "Nhà\tHouse (from the TSV).\n";

  // "đà lạt" (lower case, as selected mid-sentence) finds the "Đà Lạt" entry.
  auto m = books::lookupDefinitions("đà lạt,");
  assert(!m.empty() && m[0].headword == "Đà Lạt");
  // TSV with a Vietnamese capital.
  m = books::lookupDefinitions("NHÀ");
  assert(!m.empty() && m.back().source == "dictionary.tsv");
  // A phrase with no entry falls back to its first word.
  m = books::lookupDefinitions("zebra crossing");
  assert(!m.empty() && m[0].headword == "zebra");
  assert(books::lookupDefinitions("nothing here").empty());
  // Turning a dictionary off.
  assert(books::dictionaryNames().size() == 2);
  assert(books::saveDisabledDictionaries({"Plain test dictionary"}));
  assert(books::lookupDefinitions("zebra").empty());
  assert(books::saveDisabledDictionaries({}));
  assert(!books::lookupDefinitions("zebra").empty());
  // Vocabulary log: tabs in the data never split a line; the clock may be unset.
  assert(books::logVocabulary("đà\tlạt", "Đà Lạt", "A Book", 1795000000));
  assert(books::logVocabulary("zebra", "zebra", "Another", 0));
  std::ifstream log(books::vocabularyFile());
  std::string a, b;
  std::getline(log, a);
  std::getline(log, b);
  assert(a.find("\tđà lạt\tĐà Lạt\tA Book") != std::string::npos && a.size() > 20);
  assert(b == "\tzebra\tzebra\tAnother");
  std::system(("rm -rf " + state).c_str());
  std::puts("Books dictionary lookup, choices and vocabulary: PASS");
  return 0;
}
