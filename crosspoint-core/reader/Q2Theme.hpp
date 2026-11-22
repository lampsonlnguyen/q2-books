// SPDX-License-Identifier: MIT
#pragma once
// Q2 Pod's look for Books. Q2 Pod starts a card game with Q2_POPUP set to
// its theme's pop-up colours (docs/librespot.md in q2-pod):
//   "<style> <box> <frame> <title> <text> <pick> <pick_text>" RRGGBB each,
// as drawn, Light Mode included; style classic adds 32 colours for the title
// strip's rows and 32 for the chosen row's band. Books draws its pages,
// bars, selections and keys in those colours, so it looks like the system:
//   classic  paper panel, glossy title strip, gradient selection band
//   modern   dark page, rounded accent bar on the chosen row
//   minimal  dark page, a dot beside the chosen row
// Without the variable (older firmware, tests) the app keeps its own look.
// BOOKS_THEME (same format) overrides it, for tests and screenshots.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "HalDisplay.h"

namespace books {

struct UiTheme {
  enum class Style { None, Classic, Modern, Minimal };
  Style style = Style::None;
  uint32_t box = 0xffffff, frame = 0, title = 0, text = 0, pick = 0, pickText = 0xffffff;
  std::vector<uint32_t> bar, band;  // Classic: 32 rows each
  bool active() const { return style != Style::None; }
};

inline UiTheme parseUiTheme(const char* spec) {
  UiTheme t;
  if (!spec || !*spec) return t;
  char style[16] = {};
  unsigned c[6] = {};
  int used = 0;
  if (std::sscanf(spec, "%15s %x %x %x %x %x %x%n", style, &c[0], &c[1], &c[2], &c[3], &c[4], &c[5], &used) != 7)
    return t;
  for (unsigned v : c)
    if (v > 0xffffff) return t;
  if (!std::strcmp(style, "classic")) t.style = UiTheme::Style::Classic;
  else if (!std::strcmp(style, "modern")) t.style = UiTheme::Style::Modern;
  else if (!std::strcmp(style, "minimal")) t.style = UiTheme::Style::Minimal;
  else return t;
  t.box = c[0], t.frame = c[1], t.title = c[2], t.text = c[3], t.pick = c[4], t.pickText = c[5];
  if (t.style == UiTheme::Style::Classic) {
    const char* p = spec + used;
    std::vector<uint32_t> rows;
    unsigned v = 0;
    int n = 0;
    while (rows.size() < 64 && std::sscanf(p, " %x%n", &v, &n) == 1 && v <= 0xffffff) {
      rows.push_back(v);
      p += n;
    }
    if (rows.size() == 64) {  // "more fields may follow": ignore them
      t.bar.assign(rows.begin(), rows.begin() + 32);
      t.band.assign(rows.begin() + 32, rows.end());
    } else {  // a classic theme without its stops: flat colours
      t.bar.assign(1, t.box);
      t.band.assign(1, t.pick);
    }
  }
  return t;
}

inline const UiTheme& uiTheme() {
  static const UiTheme theme = [] {
    const char* test = std::getenv("BOOKS_THEME");
    return parseUiTheme(test && *test ? test : std::getenv("Q2_POPUP"));
  }();
  return theme;
}

// The page colours: Classic's paper and ink, a dark theme's box and title.
// |list|: a list page (the shelf), where Minimal shows unchosen rows in its
// quiet grey, as Q2 Pod's own lists.
inline void applyThemePalette(HalDisplay& display, bool list = false) {
  const auto& t = uiTheme();
  if (t.active()) display.setPalette(list && t.style == UiTheme::Style::Minimal ? t.text : t.title, t.box);
}
// Ink pixels straight into the 1-bit canvas (0 = ink there).
inline void inkPixel(HalDisplay& display, int x, int y) {
  uint8_t* fb = display.getFrameBuffer();
  if (!fb || x < 0 || y < 0 || x >= HalDisplay::DISPLAY_WIDTH || y >= HalDisplay::DISPLAY_HEIGHT) return;
  fb[y * HalDisplay::DISPLAY_WIDTH_BYTES + x / 8] &= static_cast<uint8_t>(~(0x80u >> (x & 7)));
}
// Classic's chevron on the chosen row: a 3 px ">" about 8 x 14.
inline void drawChevron(HalDisplay& display, int right, int y, int h) {
  const int cx = right - 18, cy = y + h / 2;
  for (int i = 0; i <= 7; ++i)
    for (int t = 0; t < 3; ++t) {
      inkPixel(display, cx + i + t, cy - 7 + i);
      inkPixel(display, cx + i + t, cy + 7 - i);
    }
}

inline void xorRect(HalDisplay& display, int x, int y, int w, int h) {
  uint8_t* fb = display.getFrameBuffer();
  if (!fb) return;
  for (int py = y < 0 ? 0 : y; py < y + h && py < HalDisplay::DISPLAY_HEIGHT; ++py)
    for (int px = x < 0 ? 0 : x; px < x + w && px < HalDisplay::DISPLAY_WIDTH; ++px)
      fb[py * HalDisplay::DISPLAY_WIDTH_BYTES + px / 8] ^= static_cast<uint8_t>(0x80u >> (px & 7));
}

// The chosen row (its text already drawn as ink on paper). Without a theme:
// inverted, as before.
inline void paintSelection(HalDisplay& display, int x, int y, int w, int h) {
  const auto& t = uiTheme();
  if (!t.active()) return xorRect(display, x, y, w, h);
  HalDisplay::ColorZone z;
  z.x = static_cast<int16_t>(x), z.y = static_cast<int16_t>(y), z.w = static_cast<int16_t>(w),
  z.h = static_cast<int16_t>(h);
  switch (t.style) {
    case UiTheme::Style::Classic:
      z.ink = t.pickText, z.paper = t.pick;
      z.paperRows = t.band.data(), z.rowCount = static_cast<uint8_t>(t.band.size());
      if (w >= 200) drawChevron(display, x + w, y, h);  // a list row, not a key
      break;
    case UiTheme::Style::Modern:
      z.ink = t.pickText, z.paper = t.pick, z.radius = 6;
      break;
    default:
      if (w < 100) {  // a key or a letter: too small for a dot beside it, so a light chip
        z.ink = t.box, z.paper = t.pick, z.radius = 4;
        break;
      }
      {  // Minimal: the text in the chosen colour, and a dot before it
      z.ink = t.pickText, z.paper = t.box;
      display.addZone(z);
      HalDisplay::ColorZone dot;
      const int d = h >= 20 ? 8 : 6;
      dot.x = static_cast<int16_t>(x > d + 2 ? x - d - 1 : x + 1), dot.y = static_cast<int16_t>(y + (h - d) / 2);
      dot.w = dot.h = static_cast<int16_t>(d), dot.radius = static_cast<uint8_t>(d / 2);
      dot.ink = dot.paper = t.pick;
      display.addZone(dot);
      return;
      }
  }
  display.addZone(z);
}

// A highlighted word on the page: the theme's accent (Classic's band
// colour) behind it; without a theme, inverted.
inline void paintHighlight(HalDisplay& display, int x, int y, int w, int h) {
  const auto& t = uiTheme();
  // Past the zone budget (a page full of highlights): inverted, still visible.
  if (!t.active() || display.zoneCount() >= HalDisplay::kMaxZones - 8) return xorRect(display, x, y, w, h);
  HalDisplay::ColorZone z;
  z.x = static_cast<int16_t>(x), z.y = static_cast<int16_t>(y), z.w = static_cast<int16_t>(w),
  z.h = static_cast<int16_t>(h), z.radius = 3;
  z.paper = t.pick;
  z.ink = t.pickText != t.pick ? t.pickText : t.box;  // Minimal's dot is white, as its text
  display.addZone(z);
}

// A picture (a book cover): black on white in every theme, so a dark
// theme does not show it as a negative.
inline void paintPicture(HalDisplay& display, int x, int y, int w, int h) {
  const auto& t = uiTheme();
  if (!t.active()) return;
  HalDisplay::ColorZone z;
  z.x = static_cast<int16_t>(x), z.y = static_cast<int16_t>(y), z.w = static_cast<int16_t>(w),
  z.h = static_cast<int16_t>(h);
  z.ink = 0x000000, z.paper = 0xffffff;
  display.addZone(z);
}

// A title or footer strip (its text already drawn as ink on paper). Without
// a theme: inverted (light text on a dark strip), as before.
inline void paintBar(HalDisplay& display, int x, int y, int w, int h) {
  const auto& t = uiTheme();
  if (!t.active()) return xorRect(display, x, y, w, h);
  HalDisplay::ColorZone z;
  z.x = static_cast<int16_t>(x), z.y = static_cast<int16_t>(y), z.w = static_cast<int16_t>(w),
  z.h = static_cast<int16_t>(h);
  if (t.style == UiTheme::Style::Classic) {
    z.ink = t.title, z.paper = t.box;
    z.paperRows = t.bar.data(), z.rowCount = static_cast<uint8_t>(t.bar.size());
  } else {  // Modern: a dark strip; Minimal: no strip, as its own pages
    z.ink = t.title, z.paper = t.style == UiTheme::Style::Minimal ? t.box : t.frame;
  }
  display.addZone(z);
  HalDisplay::ColorZone line;  // the hairline under a strip
  line.x = z.x, line.y = static_cast<int16_t>(y + h - 1), line.w = z.w, line.h = 1;
  line.ink = line.paper = t.style == UiTheme::Style::Classic ? t.frame : t.frame;
  if (y == 0) display.addZone(line);
}

// A keypad key: function keys as the theme's bar, the action key (Done) as
// its selection. Without a theme: function keys inverted, as before.
inline void paintKey(HalDisplay& display, int x, int y, int w, int h, bool action) {
  const auto& t = uiTheme();
  if (!t.active()) return xorRect(display, x, y, w, h);
  if (action) return paintSelection(display, x, y, w, h);
  HalDisplay::ColorZone z;
  z.x = static_cast<int16_t>(x), z.y = static_cast<int16_t>(y), z.w = static_cast<int16_t>(w),
  z.h = static_cast<int16_t>(h), z.radius = 5;
  z.ink = t.title, z.paper = t.frame;
  if (t.style == UiTheme::Style::Classic) z.paperRows = t.bar.data(), z.rowCount = static_cast<uint8_t>(t.bar.size());
  display.addZone(z);
}

// A panel's outline (menus, dialogs): the theme's frame colour.
inline void paintFrame(HalDisplay& display, int x, int y, int w, int h, int thickness) {
  const auto& t = uiTheme();
  if (!t.active()) return;
  HalDisplay::ColorZone z;
  z.ink = z.paper = t.frame;
  const int sides[4][4] = {{x, y, w, thickness}, {x, y + h - thickness, w, thickness},
                           {x, y, thickness, h}, {x + w - thickness, y, thickness, h}};
  for (const auto& s : sides) {
    z.x = static_cast<int16_t>(s[0]), z.y = static_cast<int16_t>(s[1]), z.w = static_cast<int16_t>(s[2]),
    z.h = static_cast<int16_t>(s[3]);
    display.addZone(z);
  }
}

}  // namespace books
