// SPDX-License-Identifier: MIT
// Q2 Pod's list geometry for Books' menus: a 32 px bar on top (the title,
// the clock and the battery, as the system bar), eight 32 px rows, the
// selection full width, and a 32 px footer for the position and hints.
// One place for the painters and the touch mapping, so they cannot drift.
#pragma once
#include <algorithm>

namespace books {
struct MenuLayout {
  static constexpr int kWidth = 375;
  static constexpr int kBarH = 32;
  static constexpr int kRowH = 32;
  static constexpr int kRows = 8;
  static constexpr int kTop = kBarH;                    // first row's top
  static constexpr int kFooterY = kTop + kRows * kRowH;  // 288
  static constexpr int kTextX = 14;                     // label column
  static constexpr int kValueRight = 340;               // values end here, clear of Classic's chevron

  // First visible item: the first screen until the selection passes its last
  // row, then the selection stays on the last row (as Books' lists did).
  static int first(int selected, int count) {
    if (count <= kRows) return 0;
    selected = std::clamp(selected, 0, count - 1);
    return std::clamp(selected - kRows + 1, 0, count - kRows);
  }
  static int rowY(int row) { return kTop + row * kRowH; }
  // The item under y, or -1 above or below the rows, or past the last item.
  static int itemAt(int firstItem, int count, int y) {
    if (y < kTop || y >= kFooterY) return -1;
    const int item = firstItem + (y - kTop) / kRowH;
    return item < count ? item : -1;
  }
  // Vertical offset that centres text of height h in a row.
  static int textTop(int rowTop, int h) { return rowTop + std::max(0, (kRowH - h) / 2); }
};
}  // namespace books
