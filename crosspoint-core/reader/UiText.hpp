// SPDX-License-Identifier: MIT
// Menu, bar and footer text in Q2 Pod's own TrueType faces, drawn with full
// (8-bit) anti-aliasing into the display's coverage layer, at fixed sizes:
// the reading font and size never change it.
//
// Faces, in Q2 Pod's own fallback order (patch/script.c script_face): the
// theme's face (Classic: classic-light.ttf; Modern: bold.ttf; Minimal and no
// theme: stock default.ttf), then bold.ttf, then stock default.ttf (which has
// CJK), then a language pack's font (/mnt/data/q2-font.ttf, e.g. Thai). A
// size is Q2 Pod's px: the face's ascent plus descent, as stb_truetype's
// ScaleForPixelHeight, which Q2 Pod uses.
//
// BOOKS_UI_FONT_DIR overrides the font folder (tests); BOOKS_UI_TEXT=0 turns
// it off. Without FreeType (BOOKS_HAVE_FREETYPE unset) or without any face,
// available() is false and callers keep their bitmap-font path.
#pragma once
#include <HalDisplay.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>

#if BOOKS_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

namespace books {

class UiText {
 public:
  enum class Face { Regular, Bold, Classic };

  // The theme's face; call before first use (or again when it changes).
  static UiText& instance() {
    static UiText text;
    return text;
  }
  void setThemeFace(Face face) {
    if (face == face_ && loaded_) return;
    face_ = face;
    load();
  }
  bool available() {
    if (!loaded_) load();
    return !faces_.empty();
  }

  // Advance width of utf8 at px.
  int width(const std::string& utf8, int px) {
    int w = 0;
    forEachGlyph(utf8, px, [&](const Glyph& g, int) { w += g.advance; return true; });
    return w;
  }
  int ascent(int px) { return metrics(px).ascent; }
  int lineHeight(int px) { return metrics(px).height; }
  // True when a face has every character of utf8.
  bool drawable(const std::string& utf8) {
    if (!available()) return false;
    const auto* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
    while (*p) {
      const uint32_t cp = next(&p);
      if (cp > 0x20 && faceFor(cp) < 0) return false;
    }
    return true;
  }
  bool has(uint32_t cp) { return available() && faceFor(cp) >= 0; }

  // Draws utf8 with its top at y (the line's top: baseline = y + ascent),
  // stopping before a glyph that would pass maxX. Returns the end x.
  int draw(const HalDisplay& display, int x, int top, const std::string& utf8, int px, int maxX) {
    const int baseline = top + ascent(px);
    forEachGlyph(utf8, px, [&](const Glyph& g, int) {
      if (x + g.advance > maxX + 1) return false;
      if (!g.pixels.empty())
        display.drawCoverage(x + g.left, baseline - g.top, g.width, g.height, g.pixels.data(), g.width);
      x += g.advance;
      return true;
    });
    return x;
  }

 private:
  struct Glyph {
    int width = 0, height = 0, left = 0, top = 0, advance = 0;
    std::vector<uint8_t> pixels;
  };
  struct Metrics {
    int ascent = 0, height = 0;
  };

  UiText() = default;
  ~UiText() { unload(); }

  static std::string fontDir() {
    const char* dir = std::getenv("BOOKS_UI_FONT_DIR");
    return dir && *dir ? dir : "/release/assets/default/raw/fonts";
  }
  static uint32_t next(const unsigned char** p) {
    const unsigned char* s = *p;
    uint32_t cp = *s++;
    int extra = cp >= 0xf0 ? 3 : cp >= 0xe0 ? 2 : cp >= 0xc0 ? 1 : 0;
    if (extra) cp &= 0x3fu >> extra;
    while (extra-- > 0 && (*s & 0xc0) == 0x80) cp = (cp << 6) | (*s++ & 0x3f);
    *p = s;
    return cp;
  }

  template <typename Fn>
  void forEachGlyph(const std::string& utf8, int px, Fn&& fn) {
    if (!available() || px <= 0) return;
    const auto* p = reinterpret_cast<const unsigned char*>(utf8.c_str());
    while (*p) {
      const uint32_t cp = next(&p);
      const Glyph* g = glyph(cp, px);
      if (g && !fn(*g, static_cast<int>(cp))) return;
    }
  }

#if BOOKS_HAVE_FREETYPE
  void unload() {
    cache_.clear();
    metrics_.clear();
    for (FT_Face f : faces_) FT_Done_Face(f);
    faces_.clear();
    if (library_) FT_Done_FreeType(library_);
    library_ = nullptr;
  }
  void load() {
    unload();
    loaded_ = true;
    const char* off = std::getenv("BOOKS_UI_TEXT");
    if (off && std::strcmp(off, "0") == 0) return;
    if (FT_Init_FreeType(&library_) != 0) {
      library_ = nullptr;
      return;
    }
    const std::string dir = fontDir();
    const char* first = face_ == Face::Classic ? "classic-light.ttf" : face_ == Face::Bold ? "bold.ttf" : "default.ttf";
    std::vector<std::string> paths{dir + "/" + first, dir + "/bold.ttf", dir + "/default.ttf"};
    const char* pack = std::getenv("BOOKS_UI_PACK_FONT");
    paths.push_back(pack && *pack ? pack : "/mnt/data/q2-font.ttf");
    for (size_t i = 0; i < paths.size(); ++i) {
      bool seen = false;
      for (size_t j = 0; j < i; ++j) seen = seen || paths[j] == paths[i];
      if (seen) continue;
      FT_Face face = nullptr;
      if (FT_New_Face(library_, paths[i].c_str(), 0, &face) == 0) faces_.push_back(face);
    }
  }
  int faceFor(uint32_t cp) {
    for (size_t i = 0; i < faces_.size(); ++i)
      if (FT_Get_Char_Index(faces_[i], cp)) return static_cast<int>(i);
    return -1;
  }
  bool setSize(FT_Face face, int px) {
    // Q2 Pod's px: ascent + descent = px (stb_truetype ScaleForPixelHeight).
    FT_Size_RequestRec req{};
    req.type = FT_SIZE_REQUEST_TYPE_REAL_DIM;
    req.height = static_cast<FT_Long>(px) * 64;
    return FT_Request_Size(face, &req) == 0;
  }
  Metrics metrics(int px) {
    auto it = metrics_.find(px);
    if (it != metrics_.end()) return it->second;
    Metrics m;
    if (!faces_.empty() && setSize(faces_[0], px)) {
      m.ascent = static_cast<int>(faces_[0]->size->metrics.ascender >> 6);
      m.height = static_cast<int>((faces_[0]->size->metrics.ascender - faces_[0]->size->metrics.descender) >> 6);
    }
    metrics_[px] = m;
    return m;
  }
  const Glyph* glyph(uint32_t cp, int px) {
    const uint64_t key = (uint64_t(cp) << 8) | uint64_t(px & 0xff);
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;
    if (cache_.size() >= kCacheMax) cache_.clear();  // bounded: menus use few glyphs
    Glyph g;
    int index = faceFor(cp);
    if (index < 0) index = faceFor(0xfffd);
    if (index >= 0) {
      FT_Face face = faces_[static_cast<size_t>(index)];
      if (setSize(face, px) &&
          FT_Load_Char(face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) == 0) {
        const FT_GlyphSlot slot = face->glyph;
        const FT_Bitmap& bm = slot->bitmap;
        g.advance = static_cast<int>((slot->advance.x + 32) >> 6);
        g.left = slot->bitmap_left;
        g.top = slot->bitmap_top;
        if (bm.pixel_mode == FT_PIXEL_MODE_GRAY && bm.width > 0 && bm.rows > 0) {
          g.width = static_cast<int>(bm.width);
          g.height = static_cast<int>(bm.rows);
          g.pixels.resize(size_t(g.width) * g.height);
          for (int y = 0; y < g.height; ++y)
            std::memcpy(&g.pixels[size_t(y) * g.width], bm.buffer + y * bm.pitch, size_t(g.width));
        }
      }
    }
    return &(cache_[key] = std::move(g));
  }
  FT_Library library_ = nullptr;
  std::vector<FT_Face> faces_;
#else
  void unload() {}
  void load() { loaded_ = true; }
  int faceFor(uint32_t) { return -1; }
  Metrics metrics(int) { return {}; }
  const Glyph* glyph(uint32_t, int) { return nullptr; }
  std::vector<int> faces_;
#endif
  static constexpr size_t kCacheMax = 1024;
  Face face_ = Face::Regular;
  bool loaded_ = false;
  std::unordered_map<uint64_t, Glyph> cache_;
  std::unordered_map<int, Metrics> metrics_;
};

}  // namespace books
