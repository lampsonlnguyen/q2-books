// SPDX-License-Identifier: MIT
#pragma once
// M4 folder browsing: the library seen as the card's folders. Built from the
// scanned book paths alone (no second walk of the card): a folder is listed
// when it holds a book somewhere below it, so empty folders don't show.
#include "ShelfSort.hpp"
#include <algorithm>
#include <string>
#include <vector>

namespace books {
struct ShelfFolderView {
  std::vector<std::string> folders;  // names of the folder's child folders
  std::vector<size_t> books;         // indexes of the books directly in it
};
// |path|'s folder relative to |root| ("" for the root, "a/b" below it);
// false when |path| is not under |root|.
inline bool shelfRelativeFolder(const std::string& root, const std::string& path, std::string& folder) {
  if (root.empty() || path.size() <= root.size() + 1 || path.compare(0, root.size(), root) != 0 ||
      path[root.size()] != '/')
    return false;
  const std::string rest = path.substr(root.size() + 1);
  const auto slash = rest.rfind('/');
  folder = slash == std::string::npos ? std::string() : rest.substr(0, slash);
  return true;
}
// The child folders and books of |folder| ("" is |root|). Books keep the
// order they have in |paths| (the shelf's sort); folders are A-Z.
template <class Entry>
inline ShelfFolderView shelfFolderView(const std::vector<Entry>& entries, const std::string& root,
                                       const std::string& folder) {
  ShelfFolderView view;
  const std::string prefix = folder.empty() ? std::string() : folder + "/";
  for (size_t i = 0; i < entries.size(); ++i) {
    std::string where;
    if (!shelfRelativeFolder(root, entries[i].path, where)) continue;
    if (where == folder) {
      view.books.push_back(i);
      continue;
    }
    if (where.size() <= prefix.size() || where.compare(0, prefix.size(), prefix) != 0) continue;
    const std::string below = where.substr(prefix.size());
    const std::string child = below.substr(0, below.find('/'));
    if (std::find(view.folders.begin(), view.folders.end(), child) == view.folders.end())
      view.folders.push_back(child);
  }
  std::sort(view.folders.begin(), view.folders.end(),
            [](const std::string& a, const std::string& b) {
              const int c = compareShelfNames(a, b);
              return c ? c < 0 : a < b;
            });
  return view;
}
inline std::string shelfParentFolder(const std::string& folder) {
  const auto slash = folder.rfind('/');
  return slash == std::string::npos ? std::string() : folder.substr(0, slash);
}
// "Books > Fiction > Sci-fi", the left end cut ("... > Sci-fi") past
// |maxBytes| so the folder you are in always shows.
inline std::string shelfBreadcrumb(const std::string& folder, size_t maxBytes = 34) {
  std::vector<std::string> parts{"Books"};
  for (size_t start = 0; start < folder.size();) {
    const auto slash = folder.find('/', start);
    const auto end = slash == std::string::npos ? folder.size() : slash;
    parts.push_back(folder.substr(start, end - start));
    start = end + 1;
  }
  const std::string sep = " > ";
  std::string out = parts.back();
  size_t first = parts.size() - 1;
  // the "... > " in front counts too, unless the next part is the last one
  while (first > 0 &&
         out.size() + sep.size() + parts[first - 1].size() + (first > 1 ? 3 + sep.size() : 0) <= maxBytes) {
    --first;
    out = parts[first] + sep + out;
  }
  if (first > 0) out = "..." + sep + out;
  return out;
}
}  // namespace books
