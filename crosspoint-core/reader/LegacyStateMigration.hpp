// SPDX-License-Identifier: MIT
#pragma once
// The app was renamed Books (id "books", folder .books). Earlier releases kept
// the reader's state in the card folder of the old id. On first start, the
// user's own files are copied from there into .books, their file headers
// moved to the new name; then the old folder is left as it was (its old
// program files are not used). This file is the only place the old id is
// named. Cache, fonts and program files are not copied: they are rebuilt or
// shipped with the new folder.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <string>
#include <vector>

namespace books {

inline constexpr const char* kLegacyStateDir = "/mnt/mmc/.crosspod";  // the app's former id
inline constexpr const char* kLegacyHeader = "CROSSPOD";              // its file headers' prefix
inline constexpr const char* kMigratedMark = ".migrated-from-legacy";

// Whether |name| (a file in the old folder) is the user's state.
inline bool legacyUserFile(const std::string& name) {
  static const char* const exact[] = {"reading-settings.conf", "typography.conf", "recent-books.conf",
                                      "connected.conf", "dictionary.tsv"};
  for (const char* e : exact)
    if (name == e) return true;
  return name.rfind("progress-", 0) == 0 && name.find(".part") == std::string::npos &&
         name.find(".tmp") == std::string::npos;
}

// Copy |from| to |to|, writing a leading legacy header as the new one.
inline bool copyMigratedFile(const std::string& from, const std::string& to) {
  FILE* in = std::fopen(from.c_str(), "rb");
  if (!in) return false;
  std::string data;
  char buffer[8192];
  for (size_t n; (n = std::fread(buffer, 1, sizeof buffer, in)) > 0;) {
    data.append(buffer, n);
    if (data.size() > (8u << 20)) {  // no user state file is this large
      std::fclose(in);
      return false;
    }
  }
  std::fclose(in);
  const std::string legacy = kLegacyHeader;
  if (data.compare(0, legacy.size(), legacy) == 0) data.replace(0, legacy.size(), "BOOKS");
  const std::string tmp = to + ".part";
  FILE* out = std::fopen(tmp.c_str(), "wb");
  if (!out) return false;
  bool ok = std::fwrite(data.data(), 1, data.size(), out) == data.size();
  ok = std::fclose(out) == 0 && ok;
  if (!ok || std::rename(tmp.c_str(), to.c_str()) != 0) {
    std::remove(tmp.c_str());
    return false;
  }
  return true;
}

// Run once per card: returns the number of files brought over. Files that
// already exist in |to| are kept (the new folder wins).
inline int migrateLegacyState(const std::string& from = kLegacyStateDir,
                              const std::string& to = "/mnt/mmc/.books") {
  struct stat st {};
  if (::stat(to.c_str(), &st) != 0 || !S_ISDIR(st.st_mode)) return 0;
  const std::string mark = to + "/" + kMigratedMark;
  if (::access(mark.c_str(), F_OK) == 0) return 0;
  int moved = 0;
  if (DIR* dir = opendir(from.c_str())) {
    std::vector<std::string> names;
    for (dirent* e; (e = readdir(dir)) != nullptr;) names.emplace_back(e->d_name);
    closedir(dir);
    for (const auto& name : names) {
      const std::string src = from + "/" + name, dst = to + "/" + name;
      struct stat info {};
      if (::lstat(src.c_str(), &info) != 0) continue;
      if (S_ISREG(info.st_mode) && legacyUserFile(name)) {
        if (::access(dst.c_str(), F_OK) != 0 && copyMigratedFile(src, dst)) ++moved;
      } else if (S_ISDIR(info.st_mode) && name == "dictionaries" && ::access(dst.c_str(), F_OK) != 0) {
        // Dictionaries can be large: moved, not copied (same card).
        if (std::rename(src.c_str(), dst.c_str()) == 0) ++moved;
      }
    }
  }
  if (FILE* f = std::fopen(mark.c_str(), "wb")) {
    std::fprintf(f, "%d\n", moved);
    std::fclose(f);
  }
  return moved;
}

}  // namespace books
