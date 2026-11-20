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
#include <unistd.h>

namespace {
constexpr size_t MAX_XML_BYTES = 4 * 1024 * 1024;
constexpr size_t MAX_TEXT_BYTES = 2 * 1024 * 1024;
constexpr size_t MAX_BLOCKS = 16000;
constexpr size_t MAX_PAGES_PER_SPINE = 512;
constexpr int SCREEN_H = 320, LEFT = 14, TOP = 16;
constexpr int TEXT_WIDTH = 345, LINE_HEIGHT = 27, BOTTOM = 306;
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
  std::vector<RenderPage> pages_;
  int currentSpine_ = -1;
  size_t currentPage_ = 0;

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
            if (y + LINE_HEIGHT > BOTTOM && !beginNextPage(y)) {
              pageLimitReached=true;
              return;
            }
            pages_.back().lines.push_back({std::move(line),y});
            y+=LINE_HEIGHT;
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
    std::printf("CrossPod EPUB: %s — %s | %d spine entries\n",
        info.title.c_str(),info.author.c_str(),metadata_.getSpineCount());
    return loadSpine(0,1);
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
    if (direction>0 && currentPage_+1<pages_.size()) { ++currentPage_; return true; }
    if (direction<0 && currentPage_>0) { --currentPage_; return true; }
    return loadSpine(currentSpine_+direction,direction);
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

void usage() {
  std::fputs(
    "Usage:\n"
    "  crosspod-reader --validate <book.epub>       (no framebuffer)\n"
    "  crosspod-reader --walk <book.epub>           (test pagination)\n"
    "  crosspod-reader --render <book.epub> <page.pgm> (no framebuffer)\n"
    "  crosspod-reader <book.epub>                  (Q2 Pod Books handoff)\n"
    "On Q2, only run interactively after Q2 Pod yields /dev/fb0.\n",stderr);
}
} // namespace

int main(int argc,char** argv) {
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
  reader.present();
  crosspod::Q2Controls controls;
  while(!interrupted) {
    crosspod::PhysicalEvent event{};
    if (!input.receive(event)) {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
      continue;
    }
    auto action=controls.translate(event).action;
    bool changed=false;
    if(action==crosspod::LogicalAction::Back)break;
    if(action==crosspod::LogicalAction::Next ||
       action==crosspod::LogicalAction::Confirm)changed=reader.move(1);
    if(action==crosspod::LogicalAction::Previous)changed=reader.move(-1);
    if(event.kind==crosspod::PhysicalKind::TouchUp) {
      changed=reader.move(event.x>=187?1:-1);
    }
    if(changed)reader.present();
  }
  input.close();
  return 0;
}
