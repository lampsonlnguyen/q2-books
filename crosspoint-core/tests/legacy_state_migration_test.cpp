// The rename: user files come over from the former folder once, headers
// renamed; program files and cache stay; files already in .books win.
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>

#include "LegacyStateMigration.hpp"

static void put(const std::string& p, const std::string& s) { std::ofstream(p, std::ios::binary) << s; }
static std::string get(const std::string& p) {
  std::ifstream f(p, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), {}};
}

int main() {
  char root[] = "/tmp/books-migrate-XXXXXX";
  assert(mkdtemp(root));
  const std::string from = std::string(root) + "/old", to = std::string(root) + "/new";
  assert(books::migrateLegacyState(from, to) == 0);  // no .books folder yet: nothing
  mkdir(from.c_str(), 0755);
  mkdir(to.c_str(), 0755);
  const std::string legacy = books::kLegacyHeader;
  put(from + "/reading-settings.conf", legacy + "-SETTINGS3 22 120 1 1 0 0 1 30\n");
  put(from + "/progress-0123456789abcdef", legacy + "2 10 20 3 4 o 5 6\n");
  put(from + "/progress-0123456789abcdef.cover", legacy + "-COVER1 1 2\n" + std::string("\x00\x01\xff", 3));
  put(from + "/connected.conf", "opds_url=https://example.org/opds\n");
  put(from + "/typography.conf", "kept-new");  // also present in new: the new one stays
  put(to + "/typography.conf", "BOOKS-TYPE1 serif 12\n");
  put(from + "/books-engine", "program");  // not user state
  mkdir((from + "/cache").c_str(), 0755);
  put(from + "/cache/x", "cache");
  mkdir((from + "/dictionaries").c_str(), 0755);
  put(from + "/dictionaries/a.ifo", "dict");

  assert(books::migrateLegacyState(from, to) == 5);
  assert(get(to + "/reading-settings.conf") == "BOOKS-SETTINGS3 22 120 1 1 0 0 1 30\n");
  assert(get(to + "/progress-0123456789abcdef") == "BOOKS2 10 20 3 4 o 5 6\n");
  assert(get(to + "/progress-0123456789abcdef.cover") == "BOOKS-COVER1 1 2\n" + std::string("\x00\x01\xff", 3));
  assert(get(to + "/connected.conf") == "opds_url=https://example.org/opds\n");
  assert(get(to + "/typography.conf") == "BOOKS-TYPE1 serif 12\n");
  assert(get(to + "/dictionaries/a.ifo") == "dict");
  assert(access((to + "/books-engine").c_str(), F_OK) != 0 && access((to + "/cache").c_str(), F_OK) != 0);
  assert(get(from + "/reading-settings.conf").rfind(legacy, 0) == 0);  // the old folder is left as it was
  // Once only.
  put(from + "/recent-books.conf", legacy + "-RECENT1\n");
  assert(books::migrateLegacyState(from, to) == 0);
  assert(access((to + "/recent-books.conf").c_str(), F_OK) != 0);
  std::system((std::string("rm -rf ") + root).c_str());
  std::puts("Books legacy state migration: PASS");
  return 0;
}
