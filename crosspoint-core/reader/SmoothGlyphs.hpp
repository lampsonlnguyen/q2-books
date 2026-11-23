// SPDX-License-Identifier: MIT
// Smooth book text: CrossPoint lays out every page (line breaks, word and
// glyph positions all stay its own), then, for each glyph a reading font's
// TrueType file has, this draws FreeType's full 8-bit coverage at the same
// pen position and baseline instead of the .cpfont's 2-bit edges.
//
// The TrueType files are the reading fonts' own sources, shipped unmodified
// in .books/fonts/ttf/<slug>-<style>.ttf (regular, bold, italic, bolditalic).
// The size is calibrated from the .cpfont itself: its advances (12.4 fixed
// point) across the alphabet against the TrueType's advances in font units,
// so it matches the converter's size without knowing its DPI. A style or a
// character with no TrueType keeps CrossPoint's own glyph (CJK, Indic and
// other scripts come from their own .cpfont fallbacks).
//
// BOOKS_TTF_DIR overrides the folder (tests); BOOKS_SMOOTH_GLYPHS=0 turns
// it off. Without FreeType (BOOKS_HAVE_FREETYPE unset) nothing registers.
#pragma once
#include <EpdFontFamily.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#if BOOKS_HAVE_FREETYPE
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_ADVANCES_H
#include FT_MULTIPLE_MASTERS_H
#endif

namespace books {

class SmoothGlyphs final : public GfxRenderer::GlyphHook {
 public:
  explicit SmoothGlyphs(const HalDisplay& display) : display_(display) {}
  ~SmoothGlyphs() override { clear(); }
  SmoothGlyphs(const SmoothGlyphs&) = delete;
  SmoothGlyphs& operator=(const SmoothGlyphs&) = delete;

  static bool enabled() {
#if BOOKS_HAVE_FREETYPE
    const char* v = std::getenv("BOOKS_SMOOTH_GLYPHS");
    return !(v && std::strcmp(v, "0") == 0);
#else
    return false;
#endif
  }
  static const char* styleName(EpdFontFamily::Style style) {
    switch (style) {
      case EpdFontFamily::BOLD: return "bold";
      case EpdFontFamily::ITALIC: return "italic";
      case EpdFontFamily::BOLD_ITALIC: return "bolditalic";
      default: return "regular";
    }
  }

  // Forgets every registered font (call before their data goes away).
  void clear() {
#if BOOKS_HAVE_FREETYPE
    entries_.clear();
    cache_.clear();
    for (auto& f : faces_) FT_Done_Face(f.second);
    faces_.clear();
    if (library_) FT_Done_FreeType(library_);
    library_ = nullptr;
#endif
  }
  bool empty() const { return entries_.empty(); }
  size_t size() const { return entries_.size(); }

  // Registers the reading font's style (CrossPoint data pointer and the
  // renderer font id it is drawn with) to the TrueType file at path. weight
  // (400, 700) sets a variable font's weight axis; a static file is used only
  // when ownFile (its own style's file), never to stand in for another style.
  // Returns the calibrated em size in pixels, or 0 when not registered.
  double registerStyle(const GfxRenderer& renderer, int fontId, EpdFontFamily::Style style,
                       const EpdFontData* data, const std::string& path, int weight = 400,
                       bool ownFile = true) {
#if BOOKS_HAVE_FREETYPE
    if (!data || !enabled()) return 0;
    FT_Face face = openFace(path, weight);
    if (!face) return 0;
    if (!ownFile && !FT_HAS_MULTIPLE_MASTERS(face)) return 0;
    // Calibrate: CrossPoint's advance (px, 12.4 fixed) against the TrueType
    // advance (font units) over the letters both have.
    static const char kSample[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    double px = 0, units = 0;
    for (const char* c = kSample; *c; ++c) {
      int32_t advance = 0;
      int top = 0;
      const FT_UInt index = FT_Get_Char_Index(face, static_cast<FT_ULong>(*c));
      FT_Fixed unscaled = 0;
      if (!index || !renderer.getCodepointMetrics(fontId, static_cast<uint32_t>(*c), style, advance, top) ||
          advance <= 0 || FT_Get_Advance(face, index, FT_LOAD_NO_SCALE, &unscaled) != 0 || unscaled <= 0)
        continue;
      px += advance / 16.0;
      units += static_cast<double>(unscaled);
    }
    if (units <= 0 || px <= 0) return 0;
    const double em = px / units * face->units_per_EM;
    if (em < 4 || em > 96) return 0;
    Entry entry;
    entry.face = face;
    entry.size26 = static_cast<FT_F26Dot6>(std::lround(em * 64));
    entry.id = static_cast<uint32_t>(entries_.size() + 1);
    entries_[data] = entry;
    return em;
#else
    (void)renderer, (void)fontId, (void)style, (void)data, (void)path;
    return 0;
#endif
  }

  bool wants(const EpdFontData* font, uint32_t cp) override {
#if BOOKS_HAVE_FREETYPE
    auto it = entries_.find(font);
    return it != entries_.end() && cp > 0x20 && FT_Get_Char_Index(it->second.face, cp) != 0;
#else
    (void)font, (void)cp;
    return false;
#endif
  }

  void draw(const EpdFontData* font, uint32_t cp, int penX, int baselineY) override {
#if BOOKS_HAVE_FREETYPE
    auto it = entries_.find(font);
    if (it == entries_.end()) return;
    const Glyph* g = glyph(it->second, cp);
    if (g && !g->pixels.empty())
      display_.drawCoverage(penX + g->left, baselineY - g->top, g->width, g->height, g->pixels.data(), g->width);
#else
    (void)font, (void)cp, (void)penX, (void)baselineY;
#endif
  }

  // The coverage FreeType gives cp in the font registered for data (tests
  // and the alignment check): width/height/left/top and the pixels.
  struct Glyph {
    int width = 0, height = 0, left = 0, top = 0;
    std::vector<uint8_t> pixels;
  };
#if BOOKS_HAVE_FREETYPE
  const Glyph* glyphFor(const EpdFontData* font, uint32_t cp) {
    auto it = entries_.find(font);
    return it == entries_.end() ? nullptr : glyph(it->second, cp);
  }
#endif

 private:
#if BOOKS_HAVE_FREETYPE
  struct Entry {
    FT_Face face = nullptr;
    FT_F26Dot6 size26 = 0;
    uint32_t id = 0;
  };
  // One face per file and weight: a variable font's weight axis is set on
  // the face (its other axes stay at their defaults).
  FT_Face openFace(const std::string& path, int weight) {
    const std::string key = path + "@" + std::to_string(weight);
    auto it = faces_.find(key);
    if (it != faces_.end()) return it->second;
    if (!library_ && FT_Init_FreeType(&library_) != 0) {
      library_ = nullptr;
      return nullptr;
    }
    FT_Face face = nullptr;
    if (FT_New_Face(library_, path.c_str(), 0, &face) != 0) return nullptr;
    if (FT_HAS_MULTIPLE_MASTERS(face)) {
      FT_MM_Var* mm = nullptr;
      if (FT_Get_MM_Var(face, &mm) == 0 && mm) {
        std::vector<FT_Fixed> coords(mm->num_axis);
        for (FT_UInt i = 0; i < mm->num_axis; ++i) {
          coords[i] = mm->axis[i].def;
          if (mm->axis[i].tag == FT_MAKE_TAG('w', 'g', 'h', 't')) {
            const FT_Fixed w = static_cast<FT_Fixed>(weight) << 16;
            coords[i] = std::clamp(w, mm->axis[i].minimum, mm->axis[i].maximum);
          }
        }
        FT_Set_Var_Design_Coordinates(face, mm->num_axis, coords.data());
        FT_Done_MM_Var(library_, mm);
      }
    }
    faces_[key] = face;
    return face;
  }
  const Glyph* glyph(const Entry& entry, uint32_t cp) {
    const uint64_t key = (uint64_t(entry.id) << 32) | cp;
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;
    if (cache_.size() >= kCacheMax) cache_.clear();  // a page uses a few hundred
    Glyph g;
    if (FT_Set_Char_Size(entry.face, 0, entry.size26, 72, 72) == 0 &&
        FT_Load_Char(entry.face, cp, FT_LOAD_RENDER | FT_LOAD_TARGET_LIGHT) == 0) {
      const FT_GlyphSlot slot = entry.face->glyph;
      const FT_Bitmap& bm = slot->bitmap;
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
    return &(cache_[key] = std::move(g));
  }
  FT_Library library_ = nullptr;
  std::unordered_map<std::string, FT_Face> faces_;
  std::unordered_map<const EpdFontData*, Entry> entries_;
  std::unordered_map<uint64_t, Glyph> cache_;
#else
  std::unordered_map<const EpdFontData*, int> entries_;
#endif
  static constexpr size_t kCacheMax = 2048;
  const HalDisplay& display_;
};

}  // namespace books
