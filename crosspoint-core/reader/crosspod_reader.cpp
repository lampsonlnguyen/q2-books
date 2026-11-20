// SPDX-License-Identifier: MIT
// First real EPUB-to-Q2 vertical slice: original CrossPoint ZIP/OPF, ParsedText,
// TextBlock, GfxRenderer and font rasterizer, with POSIX XML and Q2 display/input.
// XHTML structure extraction is provisional. CSS, images, ruby and links are
// NOT faithfully rendered until original ChapterHtmlSlimParser is ported.
#include <BidiUtils.h>
#include <BookMetadataCache.h>
#include <ContainerParser.h>
#include <ContentOpfParser.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ParsedText.h>
#include <Q2Controls.hpp>
#include <SdCardFont.h>
#include "Typography.hpp"
#include "ReadingPosition.hpp"
#include <Q2SocketInput.hpp>
#include <ZipFile.h>
#include <builtinFonts/notosans_12_regular.h>
#include <expat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <dirent.h>
#include <cctype>
#include <unistd.h>

namespace {
constexpr size_t MAX_XML_BYTES = 4 * 1024 * 1024;
constexpr size_t MAX_TEXT_BYTES = 2 * 1024 * 1024;
constexpr size_t MAX_BLOCKS = 16000;
constexpr size_t MAX_PAGES_PER_SPINE = 512;
constexpr int SCREEN_H = 320, LEFT = 14, TOP = 16;
constexpr int TEXT_WIDTH = 345, BOTTOM = 306;
volatile std::sig_atomic_t interrupted = 0;
void exitOnSignal(int) { interrupted = 1; }

std::string_view localName(std::string_view tag) {
  const size_t separator = tag.find_last_of("|:");
  if (separator != std::string_view::npos) tag.remove_prefix(separator + 1);
  return tag;
}
bool isParagraph(std::string_view n) {
  return n == "p" || n == "h1" || n == "h2" || n == "h3" ||
         n == "h4" || n == "h5" || n == "h6" || n == "li" ||
         n == "blockquote" || n == "pre" || n == "dt" || n == "dd";
}
bool skipTag(std::string_view n) {
  return n == "script" || n == "style" || n == "svg" || n == "nav";
}
struct Paragraph {
  std::string value;
  bool heading = false;
};
struct HtmlCollector {
  std::vector<Paragraph> items;
  std::string current;
  int depth = 0;
  int collectingDepth = 0;
  int skipDepth = 0;
  bool heading = false;
  size_t collectedBytes = 0;
  bool limitHit = false;

  static void XMLCALL start(void* user, const XML_Char* name, const XML_Char**) {
    auto& s = *static_cast<HtmlCollector*>(user);
    ++s.depth;
    const auto n = localName(name);
    if (s.skipDepth) return;
    if (skipTag(n)) { s.skipDepth = s.depth; return; }
    if (s.collectingDepth) {
      if (n == "br") s.current.append(" ");
    } else if (isParagraph(n)) {
      s.collectingDepth = s.depth;
      s.heading = n[0] == 'h' && n.size() == 2;
      s.current.clear();
    }
  }
  static void XMLCALL data(void* user, const XML_Char* text, int len) {
    auto& s = *static_cast<HtmlCollector*>(user);
    if (!s.collectingDepth || s.skipDepth || len <= 0 || s.limitHit) return;
    if (s.collectedBytes + static_cast<size_t>(len) > MAX_TEXT_BYTES) {
      s.limitHit = true;
      return;
    }
    s.current.append(text,static_cast<size_t>(len));
    s.collectedBytes += static_cast<size_t>(len);
  }
  static void XMLCALL end(void* user, const XML_Char* name) {
    auto& s = *static_cast<HtmlCollector*>(user);
    if (s.skipDepth == s.depth) s.skipDepth = 0;
    if (!s.skipDepth && s.collectingDepth == s.depth && isParagraph(localName(name))) {
      auto front = s.current.find_first_not_of(" \t\r\n");
      if (front != std::string::npos && s.items.size() < MAX_BLOCKS) {
        auto tail = s.current.find_last_not_of(" \t\r\n");
        s.items.push_back({s.current.substr(front,tail-front+1),s.heading});
      } else if (s.items.size() >= MAX_BLOCKS) s.limitHit = true;
      s.current.clear();
      s.collectingDepth = 0;
    }
    --s.depth;
  }
};
bool parseChapter(ZipFile& archive,const std::string& name,std::vector<Paragraph>& out) {
  size_t size = 0;
  if (!archive.getInflatedFileSize(name.c_str(),&size) || !size || size > MAX_XML_BYTES) {
    std::fprintf(stderr,"CrossPod: unsupported XHTML chapter length: %s (%zu)\n",name.c_str(),size);
    return false;
  }
  std::unique_ptr<uint8_t,decltype(&std::free)> xml(
      archive.readFileToMemory(name.c_str(),&size,true),&std::free);
  if (!xml) return false;
  XML_Parser parser=XML_ParserCreateNS(nullptr,'|');
  if (!parser) return false;
  HtmlCollector s;
  XML_SetUserData(parser,&s);
  XML_SetElementHandler(parser,HtmlCollector::start,HtmlCollector::end);
  XML_SetCharacterDataHandler(parser,HtmlCollector::data);
  bool ok=XML_Parse(parser,reinterpret_cast<char*>(xml.get()),
                   static_cast<int>(size),XML_TRUE)==XML_STATUS_OK;
  if (!ok) std::fprintf(stderr,"CrossPod: XHTML parse error %s: %s\n",
      name.c_str(),XML_ErrorString(XML_GetErrorCode(parser)));
  XML_ParserFree(parser);
  if (!ok || s.limitHit || s.items.empty()) return false;
  out=std::move(s.items);
  return true;
}
class TempCache {
  std::string dir_;
 public:
  TempCache() {
    char dir[]="/tmp/crosspod-epub-XXXXXX";
    if (char* d=mkdtemp(dir)) dir_=d;
  }
  const std::string& path() const { return dir_; }
  ~TempCache() {
    if (dir_.empty()) return;
    // CrossPoint cache owns temporary .items/.spine/.toc and book.bin files.
    // The private directory is only used by this process.
    for (const char* name: {"book.bin","spine.bin.tmp","toc.bin.tmp",
                            ".items.bin"}) {
      std::remove((dir_+"/"+name).c_str());
    }
    // Removal of any remaining entries is intentionally NOT recursive.
    if (rmdir(dir_.c_str()) != 0)
      std::fprintf(stderr,"CrossPod: temporary cache at %s requires cleanup\n",dir_.c_str());
  }
};

struct RenderLine {
  std::unique_ptr<TextBlock> block;
  int y = 0;
};
struct RenderPage {
  std::vector<RenderLine> lines;
};
class Reader {
  std::string bookPath_;
  ZipFile archive_;
  TempCache temporary_;
  BookMetadataCache metadata_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont bodyFont_;
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  std::unique_ptr<SdCardFont> selectedFont_;
  crosspod::Typography typography_{};
  std::string typographyNotice_;
  std::vector<RenderPage> pages_;
  int currentSpine_ = -1;
  size_t currentPage_ = 0;
  bool resumeEnabled_ = false;
  int lineHeight() const { return std::max(27,typography_.size*2); }
  bool useTypography(crosspod::Typography settings) {
    if(!crosspod::validTypography(settings))return false;
    std::unique_ptr<SdCardFont> next;
    if(settings.family!=crosspod::Typography::Family::Sans || settings.size!=12) {
      next=std::make_unique<SdCardFont>();
      if(!next->load(crosspod::typographyFontPath(settings).c_str()) ||
         !next->getEpdFont())return false;
    }
    // Rebind the original CrossPoint font registry, not a Q2 custom renderer.
    renderer_.removeFont(1);
    if(next) {
      renderer_.insertFont(1,EpdFontFamily(settings.size,next->getEpdFont()));
      renderer_.registerSdCardFont(1,next.get());
    } else renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
    selectedFont_=std::move(next);
    typography_=settings;
    return true;
  }

  bool beginNextPage(int& y) {
    if (pages_.size() >= MAX_PAGES_PER_SPINE) return false;
    pages_.emplace_back();
    y=TOP;
    return true;
  }
  bool paginate(const std::vector<Paragraph>& paragraphs) {
    pages_.clear();
    pages_.emplace_back();
    int y=TOP;
    size_t totalLines=0;
    bool pageLimitReached=false;
    for (const auto& p:paragraphs) {
      ParsedText layout(false,false,BlockStyle(),0);
      std::istringstream words(p.value);
      std::string word;
      while(words >> word) layout.addWord(word,EpdFontFamily::REGULAR);
      if (layout.hadDroppedWords()) return false;
      layout.layoutAndExtractLines(renderer_,1,TEXT_WIDTH,
          [&](std::unique_ptr<TextBlock> line,uint32_t) {
            if (!line || !line->valid() || pages_.size() > MAX_PAGES_PER_SPINE) return;
            if (y + lineHeight() > BOTTOM && !beginNextPage(y)) {
              pageLimitReached=true;
              return;
            }
            pages_.back().lines.push_back({std::move(line),y});
            y+=lineHeight();
            ++totalLines;
          });
      if (pageLimitReached) {
        std::fputs("CrossPod: chapter exceeds the bounded page cache\n",stderr);
        return false;
      }
      y += p.heading ? 12 : 8;
    }
    if (pages_.size()>1 && pages_.back().lines.empty()) pages_.pop_back();
    if (pages_.empty() || !totalLines) return false;
    return true;
  }
 public:
  explicit Reader(std::string path):
      bookPath_(std::move(path)),archive_(bookPath_),
      metadata_(temporary_.path()),renderer_(display_),
      bodyFont_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()) {}
  bool open() {
    if (temporary_.path().empty()) {
      std::fputs("CrossPod: cannot create temporary cache\n",stderr);return false;
    }
    if (!archive_.loadAllFileStatSlims()) {
      std::fputs("CrossPod: cannot index EPUB ZIP\n",stderr);return false;
    }
    size_t size=0;
    if (!archive_.getInflatedFileSize("META-INF/container.xml",&size) ||
        size>MAX_XML_BYTES || !size) return false;
    ContainerParser container(size);
    if (!container.setup() ||
        !archive_.readFileToStream("META-INF/container.xml",container,512) ||
        container.fullPath.empty()) return false;
    if (container.fullPath.front()=='/' ||
        container.fullPath.find("..")!=std::string::npos) return false;
    const std::string opfPath=container.fullPath;
    if (!archive_.getInflatedFileSize(opfPath.c_str(),&size) ||
        size>MAX_XML_BYTES || !size) return false;
    std::string opfDir;
    const auto slash=opfPath.find_last_of('/');
    if(slash!=std::string::npos) opfDir=opfPath.substr(0,slash+1);
    if(!metadata_.beginWrite() || !metadata_.beginContentOpfPass())return false;
    BookMetadataCache::BookMetadata info;
    {
      ContentOpfParser opf(temporary_.path(),opfDir,size,&metadata_,false);
      if(!opf.setup() || !archive_.readFileToStream(opfPath.c_str(),opf,1024))return false;
      info.title=opf.title;
      info.author=opf.author;
      info.language=opf.language;
      info.coverItemHref=opf.coverItemHref;
      info.textReferenceHref=opf.textReferenceHref;
    }
    if(!metadata_.endContentOpfPass() ||
       !metadata_.beginTocPass() || !metadata_.endTocPass() ||
       !metadata_.endWrite() || !metadata_.buildBookBin(bookPath_,info) ||
       !metadata_.load() || !metadata_.getSpineCount())return false;
    // These are unmodified upstream CrossPoint layout/render sources.
    display_.begin();
    renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer_.begin();
    renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
    if (!decompressor_.init()) return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    crosspod::Typography savedType;
    if(crosspod::loadTypography(savedType) && !useTypography(savedType))
      typographyNotice_="Selected font file missing";
    std::printf("CrossPod typeface: %s %dpt%s\n",\n        crosspod::typographyFamilyName(typography_.family),typography_.size,\n        typographyNotice_.empty()?"":" (saved font unavailable; fallback)");\n    std::printf("CrossPod EPUB: %s — %s | %d spine entries\n",
        info.title.c_str(),info.author.c_str(),metadata_.getSpineCount());
    return loadSpine(0,1);
  }
  void enableResume() {
    resumeEnabled_=true;
    crosspod::ReadingPosition saved;
    if(!crosspod::loadReadingPosition(bookPath_,saved))return;
    if(!loadSpine(saved.spine,1) || currentSpine_!=saved.spine) {
      (void)loadSpine(0,1);
      return;
    }
    currentPage_=std::min(saved.page,pages_.size()-1);
  }
  void persistPosition() const {
    if(resumeEnabled_)
      (void)crosspod::saveReadingPosition(bookPath_,{currentSpine_,currentPage_});
  }
  bool configureTypography(crosspod::Typography requested) {
    if(!crosspod::validTypography(requested))return false;
    if(requested.family==typography_.family && requested.size==typography_.size)return true;
    const auto previous=typography_;
    const int spine=currentSpine_;
    const size_t page=currentPage_;
    if(!useTypography(requested)) {
      typographyNotice_="Font file not installed";
      return false;
    }
    if(spine>=0 && (!loadSpine(spine,1) || currentSpine_!=spine)) {
      (void)useTypography(previous);
      (void)loadSpine(spine,1);
      typographyNotice_="Could not repaginate";
      return false;
    }
    if(spine>=0 && !pages_.empty())currentPage_=std::min(page,pages_.size()-1);
    if(!crosspod::saveTypography(typography_))
      typographyNotice_="Settings not saved to SD";
    else typographyNotice_.clear();
    persistPosition();
    return true;
  }
  crosspod::Typography typography() const {return typography_;}
  void showTypographyMenu(int selected) {
    render();
    renderer_.fillRect(13,55,349,205,false);
    renderer_.fillRect(15,57,345,201,true);
    renderer_.drawText(1,30,74,"Reading appearance");
    char row[96];
    std::snprintf(row,sizeof(row),"%s Font: %s",
      selected==0?">":" ",crosspod::typographyFamilyName(typography_.family));
    renderer_.drawText(1,31,117,row);
    std::snprintf(row,sizeof(row),"%s Size: %d",
      selected==1?">":" ",typography_.size);
    renderer_.drawText(1,31,152,row);
    renderer_.drawText(1,31,190,"Wheel: select  Center: change");
    renderer_.drawText(1,31,220,"Back: return to book");
    if(!typographyNotice_.empty())
      renderer_.drawText(1,31,245,typographyNotice_.c_str());
    display_.displayBuffer();
  }
  bool loadSpine(int index,int direction) {
    // Crossing a spine boundary must not destroy the current page if the next
    // chapter is unreadable or the user is at the beginning/end of the book.
    const int oldSpine=currentSpine_;
    const size_t oldPage=currentPage_;
    auto oldPages=std::move(pages_);
    const int n=metadata_.getSpineCount();
    while (index>=0 && index<n) {
      const auto entry=metadata_.getSpineEntry(index);
      std::vector<Paragraph> paragraphs;
      if (parseChapter(archive_,entry.href,paragraphs) && paginate(paragraphs)) {
        currentSpine_=index;
        currentPage_=direction<0 ? pages_.size()-1 : 0;
        std::printf("CrossPod: spine %d/%d %s, %zu page(s)\n",
            index+1,n,entry.href.c_str(),pages_.size());
        return true;
      }
      index+=direction;
    }
    pages_=std::move(oldPages);
    currentPage_=oldPage;
    currentSpine_=oldSpine;
    return false;
  }
  bool move(int direction) {
    if (direction>0 && currentPage_+1<pages_.size()) { ++currentPage_; persistPosition(); return true; }
    if (direction<0 && currentPage_>0) { --currentPage_; persistPosition(); return true; }
    const bool changed=loadSpine(currentSpine_+direction,direction);
    if(changed)persistPosition();
    return changed;
  }
  void render() {
    renderer_.clearScreen(0xff);
    for (const auto& line:pages_[currentPage_].lines)
      line.block->render(renderer_,1,LEFT,line.y);
  }
  void present() {
    render();
    display_.displayBuffer();
  }
  bool savePgm(const std::string& path) {
    render();
    auto* pixels=display_.getFrameBuffer();
    std::FILE* file=std::fopen(path.c_str(),"wb");
    if(!file) return false;
    bool ok=std::fprintf(file,"P5\n375 320\n255\n")>0;
    for (int y=0;ok && y<SCREEN_H;++y) {
      for (int x=0;x<375;++x) {
        bool paper=(pixels[y*47+x/8]&(0x80u>>(x&7)))!=0;
        const unsigned char c=paper?255:0;
        if(std::fwrite(&c,1,1,file)!=1){ok=false;break;}
      }
    }
    if(std::fclose(file)!=0)ok=false;
    return ok;
  }
  void report() const {
    std::printf("CrossPod validated %s: page %zu/%zu, spine %d/%d\n",
        bookPath_.c_str(),currentPage_+1,pages_.size(),
        currentSpine_+1,metadata_.getSpineCount());
  }
};


// Q2 Pod card-app launcher: the card executable receives no book-path argv.
// The Books shelf is intentionally independent of the stock reader and only
// discovers regular, non-hidden EPUB files from the SD card (one subdir deep).
struct ShelfEntry {
  std::string path;
  std::string label;
};
std::vector<ShelfEntry> scanCardBooks(const std::string& root) {
  std::vector<ShelfEntry> books;
  constexpr size_t MAX_CARD_BOOKS = 512;
  const auto visit = [&](const std::string& dir, bool nested) {
    // Iterative one-level walk: avoid recursion through attacker-controlled
    // directory trees on removable media.
    DIR* d = opendir(dir.c_str());
    if (!d) return std::vector<std::string>{};
    std::vector<std::string> subfolders;
    for (dirent* entry; (entry=readdir(d)) != nullptr;) {
      if (entry->d_name[0] == '.') continue;
      const std::string name(entry->d_name);
      if (name.size() > 240) continue;
      const std::string path=dir+"/"+name;
      if (path.size()>1000) continue;
      struct stat st{};
      if (lstat(path.c_str(),&st) != 0) continue;
      if (S_ISDIR(st.st_mode)) {
        if (!nested && subfolders.size()<128) subfolders.push_back(path);
        continue;
      }
      if (!S_ISREG(st.st_mode) || books.size()>=MAX_CARD_BOOKS) continue;
      if (name.size()<6)continue;
      const std::string_view ending(name.data()+name.size()-5,5);
      if (!(ending==".epub"||ending==".EPUB"||ending==".Epub")) continue;
      books.push_back({path,name.substr(0,name.size()-5)});
    }
    closedir(d);
    return subfolders;
  };
  const auto folders=visit(root,false);
  for(const auto& dir:folders) {
    if(books.size()>=MAX_CARD_BOOKS)break;
    (void)visit(dir,true);
  }
  const auto less=[](const ShelfEntry& a,const ShelfEntry& b){
    // ASCII case-fold only for stable sort; retain Unicode bytes intact.
    const std::string& x=a.label;
    const std::string& y=b.label;
    const size_t n=std::min(x.size(),y.size());
    for(size_t i=0;i<n;++i) {
      const unsigned char cx=static_cast<unsigned char>(x[i]);
      const unsigned char cy=static_cast<unsigned char>(y[i]);
      const auto lx=static_cast<unsigned char>(std::tolower(cx));
      const auto ly=static_cast<unsigned char>(std::tolower(cy));
      if(lx!=ly)return lx<ly;
    }
    return x.size()<y.size() || (x.size()==y.size() && a.path<b.path);
  };
  std::sort(books.begin(),books.end(),less);
  return books;
}
std::string clippedLabel(const std::string& label,size_t maxCodepoints) {
  std::string out;
  size_t pos=0,count=0;
  while(pos<label.size() && count<maxCodepoints) {
    unsigned char c=static_cast<unsigned char>(label[pos]);
    size_t len=c<0x80?1:(c>=0xC2&&c<0xE0?2:(c<0xF0?3:4));
    if(pos+len>label.size())break;
    out.append(label,pos,len);
    pos+=len;
    ++count;
  }
  if(pos<label.size())out+="...";
  return out;
}

class CardShelf {
  const std::vector<ShelfEntry>& books_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont font_;
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  size_t selected_ = 0;
  std::string notice_;
  static constexpr int TOP_Y=59, ROW_H=36, VISIBLE_ROWS=6;

 public:
  explicit CardShelf(const std::vector<ShelfEntry>& books,size_t selection=0):
      books_(books),renderer_(display_),font_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()) {
    if(!books_.empty())selected_=std::min(selection,books_.size()-1);
  }
  bool open() {
    display_.begin();
    renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer_.begin();
    renderer_.insertFont(1,EpdFontFamily(12,&font_));
    if(!decompressor_.init())return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    return true;
  }
  size_t selected() const {return selected_;}
  bool empty() const {return books_.empty();}
  const std::string& bookPath() const {return books_[selected_].path;}
  bool change(int direction) {
    if(books_.empty())return false;
    const auto before=selected_;
    if(direction>0 && selected_+1<books_.size())++selected_;
    else if(direction<0 && selected_>0)--selected_;
    notice_.clear();
    return selected_!=before;
  }
  bool tap(int x,int y) {
    if(books_.empty() || x<0||x>=375 || y<TOP_Y || y>=TOP_Y+VISIBLE_ROWS*ROW_H)return false;
    const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
    const size_t index=start+static_cast<size_t>((y-TOP_Y)/ROW_H);
    if(index>=books_.size())return false;
    selected_=index;
    return true;
  }
  void setNotice(std::string notice) {notice_=std::move(notice);}
  void paint() {
    renderer_.clearScreen(0xff);
    renderer_.fillRect(0,0,375,43,true);
    renderer_.drawText(1,17,16,"CrossPod",false);
    char counter[80];
    std::snprintf(counter,sizeof(counter),"%zu EPUB book%s",books_.size(),books_.size()==1?"":"s");
    renderer_.drawText(1,18,45,counter);
    if(books_.empty()) {
      renderer_.drawText(1,18,112,"No EPUB books found.");
      renderer_.drawText(1,18,144,"Copy books to /Books/");
    } else {
      const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
      for(size_t row=0;row<VISIBLE_ROWS && start+row<books_.size();++row) {
        const size_t index=start+row;
        const int y=TOP_Y+static_cast<int>(row)*ROW_H;
        const bool active=index==selected_;
        if(active)renderer_.fillRect(10,y+2,355,31,true);
        else renderer_.drawLine(14,y+ROW_H-3,361,y+ROW_H-3,true);
        const std::string label=clippedLabel(books_[index].label,34);
        renderer_.drawText(1,18,y+10,label.c_str(),!active);
      }
    }
    if(!notice_.empty()) {
      renderer_.fillRect(0,272,375,20,true);
      renderer_.drawText(1,12,277,clippedLabel(notice_,43).c_str(),false);
    }
    renderer_.drawText(1,18,299,"Wheel: select   Center: open   Back: exit");
  }
  void present() {paint();display_.displayBuffer();}
  bool savePgm(const std::string& out) {
    paint();
    const uint8_t* pixels=display_.getFrameBuffer();
    std::FILE* f=std::fopen(out.c_str(),"wb");
    if(!f)return false;
    bool ok=std::fprintf(f,"P5\n375 320\n255\n")>0;
    for(int y=0;y<320 && ok;++y)
      for(int x=0;x<375;++x) {
        const unsigned char color=
          (pixels[y*47+x/8]&(0x80u>>(x&7)))?255:0;
        if(std::fwrite(&color,1,1,f)!=1) {ok=false;break;}
      }
    if(std::fclose(f)!=0)ok=false;
    return ok;
  }
};

int runCardApp(const std::string& bookDir) {
  const std::vector<ShelfEntry> books=scanCardBooks(bookDir);
  crosspod::Q2SocketInput input;
  if(!input.open()) {
    std::fputs("CrossPod card: cannot bind /tmp/q2-crosspod.input.sock\n",stderr);
    return 5;
  }
  std::signal(SIGINT,exitOnSignal);
  std::signal(SIGTERM,exitOnSignal);
  size_t selection=0;
  std::unique_ptr<CardShelf> shelf(new CardShelf(books,selection));
  if(!shelf->open())return 4;
  shelf->present();
  crosspod::Q2Controls controls;
  std::unique_ptr<Reader> reader;
  bool typeMenu=false;
  int typeSelection=0;
  while(!interrupted) {
    crosspod::PhysicalEvent event{};
    if(!input.receive(event)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    const auto action=controls.translate(event).action;
    if(reader) {
      if(typeMenu) {
        if(action==crosspod::LogicalAction::Back) {
          typeMenu=false;
          reader->present();
        } else if(action==crosspod::LogicalAction::Previous ||
                  action==crosspod::LogicalAction::Next) {
          typeSelection=1-typeSelection;
          reader->showTypographyMenu(typeSelection);
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          if(event.kind==crosspod::PhysicalKind::TouchUp)
            typeSelection=event.y>=143?1:0;
          auto next=reader->typography();
          if(typeSelection==0) {
            next.family=next.family==crosspod::Typography::Family::Sans?
              crosspod::Typography::Family::Serif:crosspod::Typography::Family::Sans;
          } else next.size=next.size==12?14:(next.size==14?16:(next.size==16?18:12));
          (void)reader->configureTypography(next);
          reader->showTypographyMenu(typeSelection);
        }
        continue;
      }
      if(action==crosspod::LogicalAction::Confirm ||
         (event.kind==crosspod::PhysicalKind::TouchUp && event.y<45)) {
        typeMenu=true;
        typeSelection=0;
        reader->showTypographyMenu(typeSelection);
        continue;
      }
      if(action==crosspod::LogicalAction::Back) {
        reader.reset(); // close book/cache BEFORE reopening the card shelf
        shelf.reset(new CardShelf(books,selection));
        if(!shelf->open())return 6;
        shelf->present();
        continue;
      }
      bool changed=false;
      if(action==crosspod::LogicalAction::Next) changed=reader->move(1);
      if(action==crosspod::LogicalAction::Previous)changed=reader->move(-1);
      if(event.kind==crosspod::PhysicalKind::TouchUp)
        changed=reader->move(event.x>=187?1:-1);
      if(changed)reader->present();
      continue;
    }
    if(action==crosspod::LogicalAction::Back)break;
    if(action==crosspod::LogicalAction::Previous && shelf->change(-1))shelf->present();
    if(action==crosspod::LogicalAction::Next && shelf->change(1))shelf->present();
    bool activate=action==crosspod::LogicalAction::Confirm;
    if(event.kind==crosspod::PhysicalKind::TouchUp)activate=shelf->tap(event.x,event.y);
    if(!activate || shelf->empty())continue;
    selection=shelf->selected();
    const std::string chosen=shelf->bookPath();
    shelf->setNotice("Opening EPUB...");
    shelf->present();
    auto next=std::make_unique<Reader>(chosen);
    if(!next->open()) {
      shelf->setNotice("Cannot read this EPUB");
      shelf->present();
    } else {
      shelf.reset();
      reader=std::move(next);
      reader->enableResume();
      reader->present();
    }
  }
  input.close();
  return 0;
}

void usage() {
  std::fputs(
    "Usage:\n"
    "  crosspod-reader --validate <book.epub>       (no framebuffer)\n"
    "  crosspod-reader --walk <book.epub>           (test pagination)\n"
    "  crosspod-reader --list-books <directory>     (card scanner test)\n"
    "  crosspod-reader --card-preview <dir> <out.pgm> (menu raster test)\n"
    "  crosspod-reader (no args)                    (Q2 card app)\n"
    "  crosspod-reader --render <book.epub> <page.pgm> (no framebuffer)\n"
    "  crosspod-reader <book.epub>                  (Q2 Pod Books handoff)\n"
    "On Q2, only run interactively after Q2 Pod yields /dev/fb0.\n",stderr);
}
} // namespace

int main(int argc,char** argv) {
  // Card apps start with NO book argument; the on-device picker owns launch.
  if(argc==1) return runCardApp("/mnt/mmc/Books");
  if(argc==3 && std::strcmp(argv[1],"--list-books")==0) {
    const auto books=scanCardBooks(argv[2]);
    for(const auto& b:books)std::printf("%s\n",b.path.c_str());
    return 0;
  }
  if(argc==4 && std::strcmp(argv[1],"--card-preview")==0) {
    const auto books=scanCardBooks(argv[2]);
    CardShelf shelf(books);
    return shelf.open() && shelf.savePgm(argv[3])?0:4;
  }
  bool dry=false,render=false,walk=false;
  std::string path,output;
  if(argc==3 && std::strcmp(argv[1],"--validate")==0) {
    dry=true;path=argv[2];
  } else if(argc==3 && std::strcmp(argv[1],"--walk")==0) {
    dry=true;walk=true;path=argv[2];
  } else if(argc==4 && std::strcmp(argv[1],"--render")==0) {
    dry=true;render=true;path=argv[2];output=argv[3];
  } else if (argc==2 && argv[1][0] != '-') {
    path=argv[1];
  } else { usage();return 2; }
  if (path.size()<5 || path.substr(path.size()-5)!=".epub") {
    std::fputs("CrossPod: only EPUB is supported in this device preview\n",stderr);
    return 2;
  }
  Reader reader(std::move(path));
  if (!reader.open()) {
    std::fputs("CrossPod: EPUB not supported by current reader slice\n",stderr);
    return 3;
  }
  if(dry) {
    if(walk) {
      // Exercise real page/spine transitions and the backward boundary.
      // Limit iterations defensively for corrupt EPUBs.
      size_t forward=0,backward=0;
      while(forward<65536 && reader.move(1))++forward;
      if(forward>=65536)return 7;
      while(backward<65536 && reader.move(-1))++backward;
      if(backward>=65536||forward!=backward)return 8;
      std::printf("CrossPod walked %zu pages forward and %zu backward\n",
          forward,backward);
    }
    reader.report();
    if (render && !reader.savePgm(output)) return 4;
    return 0;
  }
  crosspod::Q2SocketInput input;
  if(!input.open()) {
    std::fputs("CrossPod: input socket unavailable, refusing interactive mode\n",stderr);
    return 5;
  }
  std::signal(SIGINT,exitOnSignal);
  std::signal(SIGTERM,exitOnSignal);
  reader.enableResume();
  reader.present();
  bool typeMenu=false;
  int typeSelection=0;
  crosspod::Q2Controls controls;
  while(!interrupted) {
    crosspod::PhysicalEvent event{};
    if (!input.receive(event)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    auto action=controls.translate(event).action;
    bool changed=false;
    if(typeMenu) {
      if(action==crosspod::LogicalAction::Back) {
        typeMenu=false;
        reader.present();
      } else if(action==crosspod::LogicalAction::Previous ||
                action==crosspod::LogicalAction::Next) {
        typeSelection=1-typeSelection;
        reader.showTypographyMenu(typeSelection);
      } else if(action==crosspod::LogicalAction::Confirm ||
                event.kind==crosspod::PhysicalKind::TouchUp) {
        if(event.kind==crosspod::PhysicalKind::TouchUp)
          typeSelection=event.y>=143?1:0;
        auto next=reader.typography();
        if(typeSelection==0)
          next.family=next.family==crosspod::Typography::Family::Sans?
            crosspod::Typography::Family::Serif:crosspod::Typography::Family::Sans;
        else next.size=next.size==12?14:(next.size==14?16:(next.size==16?18:12));
        (void)reader.configureTypography(next);
        reader.showTypographyMenu(typeSelection);
      }
      continue;
    }
    if(action==crosspod::LogicalAction::Confirm ||
       (event.kind==crosspod::PhysicalKind::TouchUp && event.y<45)) {
      typeMenu=true;
      reader.showTypographyMenu(typeSelection);
      continue;
    }
    if(action==crosspod::LogicalAction::Back)break;
    if(action==crosspod::LogicalAction::Next)changed=reader.move(1);
    if(action==crosspod::LogicalAction::Previous)changed=reader.move(-1);
    if(event.kind==crosspod::PhysicalKind::TouchUp) {
      changed=reader.move(event.x>=187?1:-1);
    }
    if(changed)reader.present();
  }
  input.close();
  return 0;
}
