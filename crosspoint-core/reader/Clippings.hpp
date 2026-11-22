// SPDX-License-Identifier: MIT
#pragma once
// M3 clippings: a book's highlights (with their text) and bookmarks as a
// plain-text file the reader can copy off the card.
//
// Highlights are stored as text offsets (Highlights.hpp); their words are
// kept beside them when they are made, in <position>.cliptext ("spine TAB
// begin TAB end TAB text" lines, capped at 256 KB). The export writes
// Clippings/<book title>.txt on the card (outside Books/, so the shelf
// never lists it as a book), replacing the previous export atomically.
#include <sys/stat.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <string>
#include <vector>

#include "Bookmarks.hpp"
#include "Highlights.hpp"
#include "ReadingPosition.hpp"

namespace books {

struct ClipText {
  uint32_t spine = 0, begin = 0, end = 0;
  std::string text;
};

inline std::string clipTextFile(const std::string& book) { return positionFile(book) + ".cliptext"; }

inline std::string flattenClipText(std::string s) {
  for (char& c : s)
    if (c == '\t' || c == '\n' || c == '\r') c = ' ';
  if (s.size() > 2000) {
    size_t cut = 2000;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
  }
  return s;
}

inline std::vector<ClipText> loadClipTexts(const std::string& book) {
  std::vector<ClipText> out;
  FILE* f = std::fopen(clipTextFile(book).c_str(), "rb");
  if (!f) return out;
  char line[2200];
  while (out.size() < 1024 && std::fgets(line, sizeof line, f)) {
    std::string s(line);
    if (s.empty() || s.back() != '\n') continue;
    s.pop_back();
    ClipText c;
    unsigned long sp = 0, b = 0, e = 0;
    int used = 0;
    if (std::sscanf(s.c_str(), "%lu\t%lu\t%lu\t%n", &sp, &b, &e, &used) != 3 || used <= 0) continue;
    c.spine = static_cast<uint32_t>(sp);
    c.begin = static_cast<uint32_t>(b);
    c.end = static_cast<uint32_t>(e);
    c.text = s.substr(static_cast<size_t>(used));
    out.push_back(std::move(c));
  }
  std::fclose(f);
  return out;
}

// Keep a highlight's words (the latest wins for the same range).
inline bool rememberClipText(const std::string& book, const ClipText& clip) {
  struct stat st {};
  if (::stat(clipTextFile(book).c_str(), &st) == 0 && st.st_size > (256 << 10)) return false;
  FILE* f = std::fopen(clipTextFile(book).c_str(), "ab");
  if (!f) return false;
  const bool ok = std::fprintf(f, "%lu\t%lu\t%lu\t%s\n", static_cast<unsigned long>(clip.spine),
                               static_cast<unsigned long>(clip.begin), static_cast<unsigned long>(clip.end),
                               flattenClipText(clip.text).c_str()) > 0;
  return std::fclose(f) == 0 && ok;
}

// File name for a title: no path separators or control characters.
inline std::string clippingFileName(const std::string& title) {
  std::string out;
  for (unsigned char c : title) {
    if (c < 0x20 || c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' ||
        c == '>' || c == '|')
      out.push_back('_');
    else out.push_back(static_cast<char>(c));
    if (out.size() >= 120 && (c & 0xC0) != 0x80) break;
  }
  while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
  if (out.empty()) out = "Book";
  return out + ".txt";
}

inline std::string clippingsFolder() {
  const char* state = std::getenv("BOOKS_STATE_DIR");
  return state && *state ? std::string(state) + "/Clippings" : std::string("/mnt/mmc/Clippings");
}

// Write the book's clippings. |chapterOf| names a spine's chapter.
template <typename ChapterName>
inline bool exportClippings(const std::string& book, const std::string& title, const std::string& author,
                            ChapterName chapterOf, std::string& written, int& count) {
  auto highlights = loadHighlights(book);
  const auto texts = loadClipTexts(book);
  const auto marks = loadBookmarks(book);
  std::sort(highlights.begin(), highlights.end(), [](const Highlight& a, const Highlight& b) {
    return a.spine != b.spine ? a.spine < b.spine : a.begin < b.begin;
  });
  std::string out = title.empty() ? "Untitled" : title;
  if (!author.empty()) out += " — " + author;
  out += "\n";
  const std::time_t now = std::time(nullptr);
  std::tm local{};
  char stamp[32] = "";
  if (now > 946684800 && localtime_r(&now, &local)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M", &local);
  if (*stamp) out += std::string("Exported ") + stamp + "\n";
  out += "\n";
  count = 0;
  for (const auto& h : highlights) {
    std::string words;
    for (auto it = texts.rbegin(); it != texts.rend(); ++it)
      if (it->spine == h.spine && it->begin == h.begin && it->end == h.end) {
        words = it->text;
        break;
      }
    out += "Highlight — " + chapterOf(static_cast<int>(h.spine)) + "\n";
    out += words.empty() ? "(text not saved: highlighted before this version)\n" : "\"" + words + "\"\n";
    out += "\n";
    ++count;
  }
  for (const auto& m : marks) {
    out += "Bookmark — " + chapterOf(m.spine) + ", page " + std::to_string(m.page + 1) + "\n\n";
    ++count;
  }
  if (count == 0) return false;
  const std::string folder = clippingsFolder();
  ::mkdir(folder.c_str(), 0755);
  written = folder + "/" + clippingFileName(title.empty() ? book.substr(book.find_last_of('/') + 1) : title);
  const std::string tmp = written + ".part";
  FILE* f = std::fopen(tmp.c_str(), "wb");
  if (!f) return false;
  bool ok = std::fwrite(out.data(), 1, out.size(), f) == out.size();
  ok = std::fflush(f) == 0 && ok;
  ok = fsync(fileno(f)) == 0 && ok;
  ok = std::fclose(f) == 0 && ok;
  if (ok && std::rename(tmp.c_str(), written.c_str()) == 0) return true;
  std::remove(tmp.c_str());
  return false;
}

}  // namespace books
