// SPDX-License-Identifier: MIT
#pragma once

#include "FontPacks.hpp"
#include "ScriptFonts.hpp"

#include <string_view>

namespace books {

// Languages that use the optional script packs. Latin, Cyrillic and other
// scripts already handled by the built-in or firmware faces need no download.
inline const FontPack* menuLanguageFontPack(std::string_view code) {
  if(code.find("_Latn")!=std::string_view::npos)return nullptr;
  const auto language=code.substr(0,code.find('_'));
  struct Match {std::string_view language,slug;};
  static constexpr Match matches[]={
    {"zh","cjk"},{"ja","cjk"},{"ko","korean"},
    {"hi","devanagari"},{"mr","devanagari"},{"ne","devanagari"},
    {"bn","bengali"},{"as","bengali"},{"gu","gujarati"},{"pa","gurmukhi"},
    {"kn","kannada"},{"ml","malayalam"},{"or","oriya"},{"si","sinhala"},
    {"ta","tamil"},{"te","telugu"},{"ar","arabic"},{"he","hebrew"},{"iw","hebrew"},
  };
  for(const auto& match:matches) {
    if(match.language!=language)continue;
    const auto slug=code=="pa_PK" || code=="pa_Arab"?std::string_view("arabic"):match.slug;
    for(const auto& pack:fontPacks)
      if(slug==pack.slug)return &pack;
  }
  return nullptr;
}

class MenuLanguageFont {
  static constexpr int fontId=450;
  std::unique_ptr<SdCardFont> font_;

 public:
  bool load(const FontPack& pack) {
    auto font=std::make_unique<SdCardFont>();
    const auto path=fontPackRoot()+"/"+pack.slug+"-12.cpfont";
    if(!usableCpfont(path) || !font->load(path.c_str()) || !font->getEpdFont())return false;
    for(const auto& script:scriptFontSpecs) {
      if(std::strcmp(script.slug,pack.slug)!=0)continue;
      if(!font->getEpdFont()->hasCodepoint(script.coverageProbe))return false;
      font_=std::move(font);
      return true;
    }
    return false;
  }

  void attach(GfxRenderer& renderer,int primary) const {
    if(!font_)return;
    renderer.removeFont(fontId);
    renderer.insertFont(fontId,sdCardFontFamily(12,*font_));
    renderer.registerSdCardFont(fontId,font_.get());
    renderer.addFallbackFont(primary,fontId);
  }

  void detach(GfxRenderer& renderer) {
    renderer.removeFont(fontId);
    font_.reset();
  }

  bool has(uint32_t cp) const {return font_ && font_->getEpdFont()->hasCodepoint(cp);}
  int glyphSlot(int primary,uint32_t cp) const {
    // Keep ASCII in the primary menu font. Scaled drawing addresses a single
    // slot, while drawText resolves fallback and shaping for the whole label.
    return cp>=0x80 && has(cp)?fontId:primary;
  }
};

}  // namespace books
