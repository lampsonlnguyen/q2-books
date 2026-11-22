// SPDX-License-Identifier: MIT
#pragma once
// M4 network covers: a catalog entry's OPDS thumbnail, fetched from the
// catalog's own origin (at most 1 MB), decoded with the reader's PNG/JPEG
// path, dithered to a 1-bit 96x128 picture and kept on the card, so a
// book's details show its cover again without the network.
#include "ConnectedConfig.hpp"
#include "JpegPreview.hpp"
#include "OpdsCatalog.hpp"
#include "PngPreview.hpp"
#include <array>
#include <cerrno>
#include <cstdio>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace books {
struct OpdsCover {
  static constexpr int width = 96, height = 128, stride = 12;
  std::array<uint8_t, stride * height> ink{};
  bool inked(int x, int y) const { return ink[static_cast<size_t>(y) * stride + x / 8] & (0x80u >> (x & 7)); }
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
// Fit |source| inside the cover, centred, with the same ordered dither as
// the shelf's thumbnails (a 1-bit LCD canvas has no grey).
inline bool makeOpdsCover(const PngPreview& source, OpdsCover& cover) {
  if (!source.width || !source.height || source.width > 1048576 / source.height ||
      source.gray.size() != static_cast<size_t>(source.width) * source.height)
    return false;
  cover.ink.fill(0);
  const unsigned w = source.width, h = source.height, W = OpdsCover::width, H = OpdsCover::height;
  const bool wide = static_cast<uint64_t>(w) * H > static_cast<uint64_t>(h) * W;
  const unsigned sw = wide ? W : std::max(1u, static_cast<unsigned>(static_cast<uint64_t>(w) * H / h));
  const unsigned sh = wide ? std::max(1u, static_cast<unsigned>(static_cast<uint64_t>(h) * W / w)) : H;
  const unsigned left = (W - sw) / 2, top = (H - sh) / 2;
  static constexpr uint8_t bayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
  for (unsigned y = 0; y < sh; ++y) {
    const unsigned sy = static_cast<unsigned>(static_cast<uint64_t>(y) * h / sh);
    for (unsigned x = 0; x < sw; ++x) {
      const unsigned sx = static_cast<unsigned>(static_cast<uint64_t>(x) * w / sw);
      const uint8_t gray = source.gray[static_cast<size_t>(sy) * w + sx];
      if (gray < static_cast<unsigned>(bayer[y & 3][x & 3]) * 16u + 8u) {
        const unsigned px = x + left, py = y + top;
        cover.ink[py * OpdsCover::stride + px / 8] |= static_cast<uint8_t>(0x80u >> (px & 7));
      }
    }
  }
  return true;
}
inline bool decodeOpdsImage(const std::vector<uint8_t>& bytes, PngPreview& out) {
  if (bytes.size() < 8) return false;
  if (decodePngPreview(bytes.data(), bytes.size(), out)) return true;
  return bytes[0] == 0xff && bytes[1] == 0xd8 && decodeJpegPreview(bytes.data(), bytes.size(), out);
}
inline bool loadOpdsCover(const std::string& href, OpdsCover& cover) {
  FILE* in = std::fopen(opdsCoverFile(href).c_str(), "rb");
  if (!in) return false;
  const std::string head = "BOOKS-OPDSCOVER1 " + href + "\n";
  std::string line(head.size(), '\0');
  bool ok = std::fread(line.data(), 1, line.size(), in) == line.size() && line == head;
  OpdsCover loaded;
  ok = ok && std::fread(loaded.ink.data(), 1, loaded.ink.size(), in) == loaded.ink.size();
  std::fclose(in);
  if (ok) cover = loaded;
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
  const std::string head = "BOOKS-OPDSCOVER1 " + href + "\n";
  bool ok = std::fwrite(head.data(), 1, head.size(), out) == head.size() &&
            std::fwrite(cover.ink.data(), 1, cover.ink.size(), out) == cover.ink.size();
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
  if (!response.ok() || !decodeOpdsImage(bytes, decoded) || !makeOpdsCover(decoded, cover)) return false;
  (void)saveOpdsCover(entry.thumbnail, cover);
  return true;
}
}  // namespace books
