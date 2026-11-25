// SPDX-License-Identifier: MIT
#pragma once
// Cache catalog thumbnails in the same RGB565 format as local EPUB covers.
#include "ConnectedConfig.hpp"
#include "CoverImage.hpp"
#include "OpdsCatalog.hpp"
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace books {
struct OpdsCover {
  static constexpr int width=96,height=128;
  FlowCover image;
};
inline constexpr size_t kOpdsCoverFiles = 256;
inline std::string opdsCoverDir() {
  const std::string conf = connectedConfigPath();
  return conf.substr(0, conf.find_last_of('/')) + "/opds-covers";
}
inline std::string opdsCoverFile(const std::string& href) {
  char name[32]{};
  std::snprintf(name, sizeof(name), "/%016llx.cover", static_cast<unsigned long long>(opdsFnv64(href)));
  return opdsCoverDir() + name;
}
inline bool loadOpdsCover(const std::string& href, OpdsCover& cover) {
  FILE* in = std::fopen(opdsCoverFile(href).c_str(), "rb");
  if (!in) return false;
  const std::string head = "BOOKS-OPDSCOVER2 " + href + "\n";
  std::string line(head.size(), '\0');
  bool ok = std::fread(line.data(), 1, line.size(), in) == line.size() && line == head;
  ok = ok && readCoverPixels(in,cover.image);
  std::fclose(in);
  return ok;
}
inline void trimOpdsCovers() {
  const std::string dir = opdsCoverDir();
  DIR* folder = ::opendir(dir.c_str());
  if (!folder) return;
  std::vector<std::pair<long long, std::string>> items;
  while (const dirent* entry = ::readdir(folder)) {
    const std::string name = entry->d_name;
    if (name.size() != 22 || name.compare(16, 6, ".cover") != 0) continue;
    struct stat info{};
    if (::lstat((dir + "/" + name).c_str(), &info) == 0 && S_ISREG(info.st_mode))
      items.emplace_back(static_cast<long long>(info.st_mtime), dir + "/" + name);
  }
  ::closedir(folder);
  if (items.size() <= kOpdsCoverFiles) return;
  std::sort(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  for (size_t i = kOpdsCoverFiles; i < items.size(); ++i) ::unlink(items[i].second.c_str());
}
inline bool saveOpdsCover(const std::string& href, const OpdsCover& cover) {
  const std::string dir = opdsCoverDir();
  if (::mkdir(dir.c_str(), 0700) != 0 && errno != EEXIST) return false;
  const std::string file = opdsCoverFile(href), temp = file + ".tmp";
  FILE* out = std::fopen(temp.c_str(), "wb");
  if (!out) return false;
  const std::string head = "BOOKS-OPDSCOVER2 " + href + "\n";
  bool ok = std::fwrite(head.data(), 1, head.size(), out) == head.size() &&
            writeCoverPixels(out,cover.image);
  ok = std::fclose(out) == 0 && ok;
  if (!ok || std::rename(temp.c_str(), file.c_str()) != 0) {
    std::remove(temp.c_str());
    return false;
  }
  trimOpdsCovers();
  return true;
}
// The entry's cover: the card's copy, else fetched (once, no retry: a
// missing cover only leaves the frame empty). False when there is none.
inline bool fetchOpdsCover(const OpdsEntry& entry, const OpdsServer& server, OpdsCover& cover) {
  if (entry.thumbnail.empty() || !parseHttpUrl(entry.thumbnail).valid) return false;
  if (loadOpdsCover(entry.thumbnail, cover)) return true;
  std::vector<uint8_t> bytes;
  constexpr size_t kMax = 1024u * 1024u;
  const auto response = httpRequest(
      entry.thumbnail,
      [&](const char* block, size_t len) {
        if (bytes.size() + len > kMax) return false;
        bytes.insert(bytes.end(), block, block + len);
        return true;
      },
      kMax, "GET", {}, opdsHeadersFor(server, entry.thumbnail));
  PngPreview decoded;
  if (!response.ok() || !decodeCoverImage(bytes.data(),bytes.size(),decoded) || !makeColorCover(decoded,cover.image)) return false;
  (void)saveOpdsCover(entry.thumbnail, cover);
  return true;
}
}  // namespace books
