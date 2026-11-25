// SPDX-License-Identifier: MIT
#pragma once

#ifdef BOOKS_HAVE_FREETYPE
#include "ShelfIndexer.hpp"
#include "UnicodeCase.hpp"
#include <EpdFontFamily.h>
#include <FsHelpers.h>
#include <Utf8.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_SYNTHESIS_H
#include FT_TRUETYPE_TABLES_H
#include <array>
#include <new>
#include <string_view>

namespace books {
class EmbeddedBookFont {
  using ArchiveBytes=std::unique_ptr<uint8_t,decltype(&std::free)>;
  struct Source {
    ArchiveBytes bytes{nullptr,&std::free};
    size_t size=0;
  };
  struct Declaration {
    std::string family,path;
    size_t style=0;
  };

  static std::string_view trim(std::string_view text) {
    const auto start=text.find_first_not_of(" \t\r\n\f");
    return start==std::string_view::npos?std::string_view():
        text.substr(start,text.find_last_not_of(" \t\r\n\f")-start+1);
  }
  static std::string_view unquote(std::string_view text) {
    text=trim(text);
    if(text.size()>=2 && (text.front()=='\'' || text.front()=='"') && text.front()==text.back())
      text=text.substr(1,text.size()-2);
    return text;
  }
  static std::string_view property(std::string_view declarations,std::string_view key) {
    while(!declarations.empty()) {
      const auto end=declarations.find(';');
      const auto entry=declarations.substr(0,end);
      const auto separator=entry.find(':');
      if(separator!=std::string_view::npos && trim(entry.substr(0,separator))==key)
        return trim(entry.substr(separator+1));
      if(end==std::string_view::npos)break;
      declarations.remove_prefix(end+1);
    }
    return {};
  }
  static std::string familyName(std::string_view text) {
    text=unquote(text.substr(0,text.find(',')));
    if(text.empty() || text.size()>96)return {};
    return utf8Lower(std::string(text));
  }
  static std::string fontPath(std::string_view source,const std::string& stylesheet,
                              const std::vector<std::string>& manifest) {
    while(!source.empty()) {
      const auto start=source.find("url(");
      if(start==std::string_view::npos)return {};
      const auto end=source.find(')',start+4);
      if(end==std::string_view::npos)return {};
      const auto raw=unquote(source.substr(start+4,end-start-4));
      source.remove_prefix(end+1);
      if(raw.empty() || raw.size()>400)continue;
      const auto decoded=FsHelpers::decodeUriEscapes(std::string(raw));
      if(decoded.empty() || decoded.front()=='/' || decoded.find_first_of(":?#\\")!=std::string::npos ||
         decoded.find('\0')!=std::string::npos)continue;
      const auto slash=stylesheet.find_last_of('/');
      const auto base=slash==std::string::npos?std::string():stylesheet.substr(0,slash+1);
      size_t depth=std::count(base.begin(),base.end(),'/');
      bool escaped=false;
      std::string_view remaining(decoded);
      while(!remaining.empty()) {
        const auto separator=remaining.find('/');
        const auto component=remaining.substr(0,separator);
        if(component=="..") {
          if(depth==0){escaped=true;break;}
          --depth;
        } else if(!component.empty() && component!=".")++depth;
        if(separator==std::string_view::npos)break;
        remaining.remove_prefix(separator+1);
      }
      if(escaped)continue;
      const auto resolved=FsHelpers::normalisePath(base+decoded);
      if(isSafeEpubItem(resolved) && std::find(manifest.begin(),manifest.end(),resolved)!=manifest.end())
        return resolved;
    }
    return {};
  }
  static void scanCss(std::string css,const std::string& stylesheet,const std::vector<std::string>& manifest,
                      std::string& documentFamily,std::vector<Declaration>& faces) {
    for(size_t start=css.find("/*");start!=std::string::npos;start=css.find("/*",start)) {
      const auto end=css.find("*/",start+2);
      if(end==std::string::npos){css.resize(start);break;}
      std::fill(css.begin()+start,css.begin()+end+2,' ');
    }
    std::string_view remaining(css);
    while(!remaining.empty()) {
      const auto open=remaining.find('{');
      const auto close=remaining.find('}');
      if(open==std::string_view::npos || close==std::string_view::npos)break;
      if(close<open) {remaining.remove_prefix(close+1);continue;}
      auto selector=trim(remaining.substr(0,open));
      if(const auto rule=selector.find_last_of(';');rule!=std::string_view::npos)
        selector=trim(selector.substr(rule+1));
      const auto declarations=remaining.substr(open+1,close-open-1);
      remaining.remove_prefix(close+1);
      if(declarations.find('{')!=std::string_view::npos)continue;
      const auto family=familyName(property(declarations,"font-family"));
      if(family.empty())continue;
      if(selector=="body" || selector=="html" || selector=="html, body" || selector=="body, html")
        documentFamily=family;
      if(selector!="@font-face" || faces.size()>=24)continue;
      const auto style=property(declarations,"font-style");
      const auto weight=property(declarations,"font-weight");
      const auto path=fontPath(property(declarations,"src"),stylesheet,manifest);
      if(!path.empty())faces.push_back({family,path,
          size_t(style=="italic" || style=="oblique")*2+
          size_t(weight=="bold" || weight=="600" || weight=="700" || weight=="800" || weight=="900")});
    }
  }

  struct Face {
    struct Slot {
      uint32_t codepoint=UINT32_MAX;
      EpdGlyph glyph{};
      std::unique_ptr<uint8_t[]> pixels;
    };

    FT_Face ft=nullptr;
    bool bold=false,italic=false;
    size_t nextSlot=0;
    std::array<Slot,24> cache{};
    EpdFontData data{};
    EpdFont font{&data};

    ~Face() {if(ft)FT_Done_Face(ft);}
    bool open(FT_Library library,const Source& source,unsigned pixels,bool synthBold,bool synthItalic) {
      if(!source.bytes || FT_New_Memory_Face(library,source.bytes.get(),source.size,0,&ft))return false;
      if(!FT_IS_SCALABLE(ft) || ft->num_faces!=1 || FT_Select_Charmap(ft,FT_ENCODING_UNICODE) ||
         FT_Set_Pixel_Sizes(ft,0,pixels))return false;
      const auto* os2=static_cast<const TT_OS2*>(FT_Get_Sfnt_Table(ft,ft_sfnt_os2));
      if(!os2 || os2->version==0xffff || (os2->fsType&0x0202))return false;
      for(uint32_t cp=32;cp<127;++cp)if(!FT_Get_Char_Index(ft,cp))return false;
      bold=synthBold;
      italic=synthItalic;
      data.advanceY=static_cast<uint8_t>(std::clamp<long>(ft->size->metrics.height/64,8,96));
      data.ascender=std::clamp<long>(ft->size->metrics.ascender/64,1,80);
      data.descender=std::clamp<long>(ft->size->metrics.descender/64,-48,0);
      data.is2Bit=true;
      data.glyphMissCtx=this;
      data.glyphMissHandler=[](void* ctx,uint32_t cp){return static_cast<Face*>(ctx)->glyph(cp);};
      data.coverageHandler=[](void* ctx,uint32_t cp){return static_cast<Face*>(ctx)->covers(cp);};
      data.vectorBitmapHandler=[](void* ctx,const EpdGlyph* glyph)->const uint8_t* {
        const auto& self=*static_cast<const Face*>(ctx);
        return glyph->dataOffset<self.cache.size()?self.cache[glyph->dataOffset].pixels.get():nullptr;
      };
      data.kernHandler=[](void* ctx,uint32_t left,uint32_t right)->int8_t {
        auto ft=static_cast<Face*>(ctx)->ft;
        FT_Vector adjustment{};
        if(FT_Get_Kerning(ft,FT_Get_Char_Index(ft,left),FT_Get_Char_Index(ft,right),
                          FT_KERNING_UNFITTED,&adjustment))return 0;
        return static_cast<int8_t>(std::clamp<long>(adjustment.x/4,-128,127));
      };
      return true;
    }
    bool covers(uint32_t cp) const {
      // Keep the existing shaper for complex scripts instead of substituting
      // an unshaped vector face simply because its cmap lists those characters.
      const bool complex=(cp>=0x0590 && cp<=0x109f) || (cp>=0x1780 && cp<=0x17ff) ||
          (cp>=0xa8e0 && cp<=0xa8ff);
      return !complex && FT_Get_Char_Index(ft,cp);
    }
    const EpdGlyph* glyph(uint32_t cp) {
      for(auto& slot:cache)if(slot.codepoint==cp)return &slot.glyph;
      auto index=FT_Get_Char_Index(ft,cp);
      if(!index && cp==0xfffd)index=FT_Get_Char_Index(ft,'?');
      if(!index || FT_Load_Glyph(ft,index,FT_LOAD_NO_HINTING|FT_LOAD_NO_BITMAP) ||
         ft->glyph->format!=FT_GLYPH_FORMAT_OUTLINE)return nullptr;
      if(bold)FT_GlyphSlot_Embolden(ft->glyph);
      if(italic)FT_GlyphSlot_Oblique(ft->glyph);
      const auto metrics=ft->glyph->metrics;
      if(metrics.width>4096 || metrics.height>4096)return nullptr;
      if(FT_Render_Glyph(ft->glyph,FT_RENDER_MODE_NORMAL))return nullptr;
      const auto& bitmap=ft->glyph->bitmap;
      if(bitmap.width>64 || bitmap.rows>64 || bitmap.pitch<0 ||
         (bitmap.width && bitmap.rows && bitmap.pixel_mode!=FT_PIXEL_MODE_GRAY))return nullptr;
      const size_t picked=nextSlot++%cache.size();
      auto& slot=cache[picked];
      slot.codepoint=UINT32_MAX;
      slot.glyph={static_cast<uint8_t>(bitmap.width),static_cast<uint8_t>(bitmap.rows),
          static_cast<uint16_t>(std::clamp<long>(ft->glyph->advance.x/4,0,2048)),
          static_cast<int16_t>(std::clamp(ft->glyph->bitmap_left,-128,128)),
          static_cast<int16_t>(std::clamp(ft->glyph->bitmap_top,-128,128)),
          static_cast<uint16_t>((bitmap.width*bitmap.rows+3)/4),static_cast<uint32_t>(picked)};
      // Allocate just the packed glyph bytes, not a 1 KiB slab for every
      // slot in every style. Empty glyphs retain a one-byte backing store.
      slot.pixels.reset(new(std::nothrow) uint8_t[std::max<size_t>(1,slot.glyph.dataLength)]{});
      if(!slot.pixels)return nullptr;
      slot.codepoint=cp;
      for(size_t y=0;y<bitmap.rows;++y)for(size_t x=0;x<bitmap.width;++x) {
        const size_t i=y*bitmap.width+x;
        const unsigned ink=(bitmap.buffer[y*bitmap.pitch+x]*3u+127u)/255u;
        slot.pixels[i/4]|=static_cast<uint8_t>(ink<<(6-i%4*2));
      }
      return &slot.glyph;
    }
  };

  FT_Library library_=nullptr;
  std::array<Source,4> sources_;
  std::array<std::unique_ptr<Face>,4> faces_;
  bool scanned_=false;

 public:
  ~EmbeddedBookFont() {
    for(auto& face:faces_)face.reset();
    if(library_)FT_Done_FreeType(library_);
  }
  void scan(const std::string& package) {
    if(scanned_)return;
    scanned_=true;
    ZipFile zip(package);
    // Named ZIP lookups avoid retaining an archive-wide index for fonts.
    size_t size=0;
    if(!zip.getInflatedFileSize("META-INF/container.xml",&size) || !size || size>256*1024)return;
    ContainerParser container(size);
    if(!container.setup() || !zip.readFileToStream("META-INF/container.xml",container,512) ||
       !isSafeEpubItem(container.fullPath))return;
    const auto& path=container.fullPath;
    if(!zip.getInflatedFileSize(path.c_str(),&size) || !size || size>1024*1024)return;
    const auto slash=path.find_last_of('/');
    ContentOpfParser opf("",slash==std::string::npos?"":path.substr(0,slash+1),size,nullptr);
    if(!opf.setup() || !zip.readFileToStream(path.c_str(),opf,1024) || opf.fontFiles.empty())return;

    std::string documentFamily;
    std::vector<Declaration> declared;
    size_t scanned=0;
    for(const auto& sheet:opf.cssFiles) {
      if(++scanned>16)break;
      if(!isSafeEpubItem(sheet) || !zip.getInflatedFileSize(sheet.c_str(),&size) ||
         !size || size>128*1024)continue;
      ArchiveBytes css(zip.readFileToMemory(sheet.c_str(),&size,false),&std::free);
      if(css) {
        std::string text(reinterpret_cast<const char*>(css.get()),size);
        css.reset();
        scanCss(std::move(text),sheet,opf.fontFiles,documentFamily,declared);
      }
    }
    if(documentFamily.empty())return;
    size_t budget=2*1024*1024;
    for(const auto& face:declared) {
      if(face.family!=documentFamily || sources_[face.style].bytes ||
         !zip.getInflatedFileSize(face.path.c_str(),&size) || size<16 ||
         size>1024*1024 || size>budget)continue;
      ArchiveBytes data(zip.readFileToMemory(face.path.c_str(),&size,false),&std::free);
      if(!data)continue;
      constexpr unsigned char ttf[]={0,1,0,0};
      if(std::memcmp(data.get(),ttf,4)!=0 && std::memcmp(data.get(),"OTTO",4)!=0)continue;
      sources_[face.style]={std::move(data),size};
      budget-=size;
    }
  }
  bool use(unsigned points) {
    for(auto& face:faces_)face.reset();
    if(!sources_[0].bytes || (!library_ && FT_Init_FreeType(&library_)))return false;
    const unsigned pixels=(points*FONT_RASTER_DPI+36)/72;
    for(size_t i=0;i<faces_.size();++i) {
      auto face=std::make_unique<Face>();
      const bool exact=sources_[i].bytes && face->open(library_,sources_[i],pixels,false,false);
      if(!exact) {
        if(!i)return false;
        face=std::make_unique<Face>();
        if(!face->open(library_,sources_[0],pixels,(i&1)!=0,(i&2)!=0))continue;
      }
      faces_[i]=std::move(face);
    }
    return true;
  }
  void releaseGlyphs() {for(auto& face:faces_)face.reset();}
  void release() {
    releaseGlyphs();
    for(auto& source:sources_)source=Source{};
    if(library_) {FT_Done_FreeType(library_);library_=nullptr;}
    scanned_=false;
  }
  size_t residentSourceBytes() const {
    size_t bytes=0;
    for(const auto& source:sources_)if(source.bytes)bytes+=source.size;
    return bytes;
  }
  size_t residentGlyphBytes() const {
    size_t bytes=0;
    for(const auto& face:faces_)if(face)
      for(const auto& slot:face->cache)if(slot.pixels)
        bytes+=std::max<size_t>(1,slot.glyph.dataLength);
    return bytes;
  }
  EpdFontFamily family(uint16_t points) const {
    const auto get=[&](size_t index){return faces_[index]?&faces_[index]->font:nullptr;};
    return EpdFontFamily(points,get(0),get(1),get(2),get(3));
  }
};
} // namespace books
#endif
