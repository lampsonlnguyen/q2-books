// SPDX-License-Identifier: MIT
// First real EPUB-to-Q2 vertical slice: original CrossPoint ZIP/OPF, ParsedText,
// TextBlock, GfxRenderer and font rasterizer, with POSIX XML and Q2 display/input.
// XHTML structure extraction is provisional. CSS, images, ruby and links are
// NOT faithfully rendered until original ChapterHtmlSlimParser is ported.
#include <BidiUtils.h>
#include <BookMetadataCache.h>
#include <ContainerParser.h>
#include <ContentOpfParser.h>
#include <CssParser.h>
#include <HalStorage.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <ParsedText.h>
#include <Page.h>
#include <Section.h>
#include <ReaderRenderSpec.h>
#include <Epub/blocks/ImageBlock.h>
#include <Q2Controls.hpp>
#include <SdCardFont.h>
#include "Typography.hpp"
#include "ScriptFonts.hpp"
#include "ReadingPosition.hpp"
#include "ReaderPreferences.hpp"
#include "ReadingLayout.hpp"
#include "Utf8Label.hpp"
#include "ShelfHistory.hpp"
#include "ShelfSort.hpp"
#include "ShelfDetails.hpp"
#include "ShelfCover.hpp"
#include "ShelfIndexer.hpp"
#include "ReaderLinks.hpp"
#include "Highlights.hpp"
#include "SelectionGeometry.hpp"
#include "OfflineDictionary.hpp"
#include "FootnotePreview.hpp"
#include "OpdsCatalog.hpp"
#include "KoreaderSync.hpp"
#include <ProgressMapper.h>
#include <ProgressComparison.h>
#include "ShelfLibrary.hpp"
#include "ShelfSearch.hpp"
#include "Bookmarks.hpp"
#include "PngPreview.hpp"
#include "JpegPreview.hpp"
#include <Q2SocketInput.hpp>
#include <ZipFile.h>
#include <builtinFonts/notosans_12_regular.h>
#include <expat.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <csignal>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <sys/resource.h>
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
std::string clippedLabel(const std::string&,size_t);
void exitOnSignal(int) { interrupted = 1; }

bool installExitHandlers() {
  struct sigaction action{};
  action.sa_handler=exitOnSignal;
  sigemptyset(&action.sa_mask);
  if(sigaction(SIGINT,&action,nullptr)==0 && sigaction(SIGTERM,&action,nullptr)==0)
    return true;
  std::perror("CrossPod: cannot install exit handlers");
  return false;
}

using ReaderClock = std::chrono::steady_clock;
constexpr auto positionSaveInterval = std::chrono::seconds(5);

// A finite ceiling also covers a shutdown signal arriving just before poll.
int waitForReaderInput(const crosspod::Q2SocketInput& input,ReaderClock::time_point deadline) {
  if(interrupted)return 0;
  const auto now=ReaderClock::now();
  const auto delay=std::min(now+std::chrono::seconds(1),deadline)-now;
  const auto timeout=std::max(std::chrono::milliseconds(0),
      std::chrono::ceil<std::chrono::milliseconds>(delay));
  return input.waitForInput(static_cast<int>(timeout.count()));
}

int loadWheelSensitivity() {
  std::unique_ptr<std::FILE,decltype(&std::fclose)> config(
      std::fopen("/mnt/data/config.ini","r"),&std::fclose);
  if(!config)return crosspod::defaultWheelSensitivity;
  const auto trim=[](std::string_view text) {
    const auto first=text.find_first_not_of(" \t\r\n");
    if(first==std::string_view::npos)return std::string_view{};
    return text.substr(first,text.find_last_not_of(" \t\r\n")-first+1);
  };
  bool inQ2Pod=false;
  char buffer[256];
  while(std::fgets(buffer,sizeof(buffer),config.get())) {
    const auto line=trim(buffer);
    if(line.starts_with('['))inQ2Pod=line=="[Q2POD]";
    const auto equals=line.find('=');
    if(inQ2Pod && equals!=std::string_view::npos &&
       trim(line.substr(0,equals))=="WHEELSENSITIVITY")
      return crosspod::parseWheelSensitivity(trim(line.substr(equals+1)));
  }
  return crosspod::defaultWheelSensitivity;
}

class SessionProfile {
  ReaderClock::time_point start_=ReaderClock::now();
  struct rusage initial_{};
  bool enabled_=false;
  static long long micros(const timeval& time) {
    return static_cast<long long>(time.tv_sec)*1000000+time.tv_usec;
  }
 public:
  SessionProfile() {
    const char* value=std::getenv("CROSSPOD_PROFILE_SESSION");
    enabled_=value && std::strcmp(value,"1")==0 && getrusage(RUSAGE_SELF,&initial_)==0;
  }
  ~SessionProfile() {
    struct rusage usage{};
    if(!enabled_ || getrusage(RUSAGE_SELF,&usage)!=0)return;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        ReaderClock::now()-start_).count();
    std::fprintf(stderr,
        "CrossPod session: elapsed_ms=%lld user_cpu_us=%lld system_cpu_us=%lld "
        "maxrss_kb=%ld read_blocks=%ld written_blocks=%ld voluntary_switches=%ld "
        "involuntary_switches=%ld\n",
        static_cast<long long>(elapsed),
        micros(usage.ru_utime)-micros(initial_.ru_utime),
        micros(usage.ru_stime)-micros(initial_.ru_stime),
        usage.ru_maxrss,usage.ru_inblock-initial_.ru_inblock,
        usage.ru_oublock-initial_.ru_oublock,
        usage.ru_nvcsw-initial_.ru_nvcsw,usage.ru_nivcsw-initial_.ru_nivcsw);
  }
};

std::string_view localName(std::string_view tag) {
  const size_t separator = tag.find_last_of("|:");
  if (separator != std::string_view::npos) tag.remove_prefix(separator + 1);
  return tag;
}
bool isParagraph(std::string_view n) {
  return n == "p" || n == "h1" || n == "h2" || n == "h3" ||
         n == "h4" || n == "h5" || n == "h6" || n == "li" ||
         n == "blockquote" || n == "pre" || n == "dt" || n == "dd" ||
         n == "div" || n == "section" || n == "article" ||
         n == "figcaption" || n == "caption" || n == "td" || n == "th";
}
bool skipTag(std::string_view n) {
  return n == "script" || n == "style" || n == "svg" || n == "nav";
}
// Resolve a relative EPUB ZIP resource without allowing traversal outside
// the archive. Relative ../ segments are common for CSS and XHTML images.
std::string resolveEpubResource(const std::string& chapterDir,std::string_view uri) {
  if(uri.empty() || uri.front()=='/' || uri.front()=='\\' ||
     uri.find(':')!=std::string_view::npos ||
     uri.find(static_cast<char>(92))!=std::string_view::npos ||
     uri.find('?')!=std::string_view::npos ||
     uri.find('#')!=std::string_view::npos)return {};
  std::vector<std::string> segments;
  const std::string joined=chapterDir+std::string(uri);
  size_t cursor=0;
  while(cursor<joined.size()) {
    const size_t next=joined.find('/',cursor);
    const auto segment=joined.substr(cursor,next==std::string::npos?
                                            std::string::npos:next-cursor);
    if(segment=="..") {
      if(segments.empty())return {};
      segments.pop_back();
    } else if(!segment.empty() && segment!=".")segments.push_back(segment);
    if(next==std::string::npos)break;
    cursor=next+1;
  }
  if(segments.empty())return {};
  std::string normalized;
  for(const auto& segment:segments) {
    if(!normalized.empty())normalized.push_back('/');
    normalized+=segment;
  }
  return normalized;
}
struct StyledRun {
  std::string text;
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
};
struct Paragraph {
  std::vector<StyledRun> runs;
  CssTextAlign align = CssTextAlign::Justify;
  bool rtl = false;
  bool heading = false;
  BlockStyle block; // original CrossPoint CSS box style
  std::string imagePath; // EPUB ZIP path, bounded PNG or JPEG resource.
};
struct HtmlCollector {
  // The full upstream ChapterHtmlSlimParser is being integrated separately.
  // This transitional adapter uses the ORIGINAL CrossPoint CSS cascade,
  // rather than interpreting CSS with a Q2-specific parser.
  ZipFile& archive;
  std::string chapterDir;
  CssParser stylesheet{""};
  std::vector<Paragraph> items;
  std::array<CssAncestor, 16> ancestors{};
  struct Scope {
    bool hidden=false, bold=false, italic=false, rtl=false;
    CssTextAlign align=CssTextAlign::Justify;
    bool paragraph=false;
    BlockStyle paragraphBlock{};
    bool heading=false;
    bool stylesheet=false;
    bool pageBreakAfter=false;
    int listKind=0; // 0 none, 1 unordered, 2 ordered
    unsigned nextItemNumber=1;
  };
  std::vector<Scope> scopes;
  Paragraph current;
  std::string cssText;
  int collectingDepth=0;
  size_t collectedBytes=0;
  bool limitHit=false;

  HtmlCollector(ZipFile& zip, const std::string& chapter) : archive(zip) {
    const size_t slash=chapter.find_last_of('/');
    if(slash!=std::string::npos) chapterDir=chapter.substr(0,slash+1);
    scopes.push_back({});
  }
  static std::string_view attribute(const XML_Char** attrs, std::string_view key) {
    if(!attrs)return {};
    for(size_t i=0;attrs[i];i+=2)
      if(attrs[i]==key)return attrs[i+1];
    return {};
  }
  bool addCss(std::string_view content) {
    if(content.empty() || content.size()>256*1024)return false;
    HalFile file(std::tmpfile());
    if(!file || file.write(content.data(),content.size())!=content.size() || !file.seek(0))
      return false;
    return stylesheet.loadFromStream(file)!=CssParser::ParseResult::Error;
  }
  void externalCss(std::string_view uri) {
    const std::string path=resolveEpubResource(chapterDir,uri);
    if(path.empty())return;
    size_t len=0;
    if(!archive.getInflatedFileSize(path.c_str(),&len)||len==0||len>256*1024)return;
    std::unique_ptr<uint8_t,decltype(&std::free)> bytes(
      archive.readFileToMemory(path.c_str(),&len,true),&std::free);
    if(bytes)(void)addCss(std::string_view(reinterpret_cast<const char*>(bytes.get()),len));
  }
  static void XMLCALL start(void* user, const XML_Char* name, const XML_Char** attrs) {
    auto& s=*static_cast<HtmlCollector*>(user);
    const auto n=localName(name);
    const auto& parent=s.scopes.back();
    Scope scope=parent;
    scope.paragraph=false;
    scope.stylesheet=false;
    scope.pageBreakAfter=false;
    if(n=="ol" || n=="ul") {
      scope.listKind=n=="ol"?2:1;
      scope.nextItemNumber=1;
      // Honor EPUB ordered-list start values with bounded numeric parsing.
      if(n=="ol") {
        const auto startValue=attribute(attrs,"start");
        if(!startValue.empty() && startValue.size()<=5) {
          unsigned number=0;
          bool valid=true;
          for(const unsigned char digit:startValue) {
            if(digit<'0' || digit>'9') {valid=false;break;}
            number=number*10+digit-'0';
          }
          if(valid && number>0 && number<=99999)scope.nextItemNumber=number;
        }
      }
    }
    const std::string_view cls=attribute(attrs,"class");
    const std::string_view id=attribute(attrs,"id");
    if(n=="link") {
      const auto rel=attribute(attrs,"rel");
      if(rel=="stylesheet")s.externalCss(attribute(attrs,"href"));
    }
    CssStyle style;
    if(!scope.hidden) {
      const size_t depth=s.scopes.size()-1;
      style=s.stylesheet.resolveStyle(n,cls,id,s.ancestors.data(),
          std::min(depth,s.ancestors.size()));
      const auto inlineCss=attribute(attrs,"style");
      if(!inlineCss.empty())style.applyOver(CssParser::parseInlineStyle(inlineCss));
      if(depth<s.ancestors.size())
        s.ancestors[depth]=CssParser::makeAncestor(n,cls,id);
    }
    scope.hidden=scope.hidden || skipTag(n) || n=="head" || n=="title" ||
      n=="rp" || !attribute(attrs,"hidden").empty() ||
      (style.hasDisplay() && style.display==CssDisplay::None);
    // A stylesheet is captured in head even though head is not visible content.
    scope.stylesheet=(n=="style");
    if(n=="b" || n=="strong")scope.bold=true;
    if(n=="i" || n=="em")scope.italic=true;
    if(style.hasFontWeight())scope.bold=style.fontWeight==CssFontWeight::Bold;
    if(style.hasFontStyle())scope.italic=style.fontStyle==CssFontStyle::Italic;
    if(style.hasTextAlign())scope.align=style.textAlign;
    if(style.hasDirection())scope.rtl=style.direction==CssTextDirection::Rtl;
    const auto dir=attribute(attrs,"dir");
    if(dir=="rtl")scope.rtl=true;
    else if(dir=="ltr")scope.rtl=false;
    if(!scope.hidden && isParagraph(n)) {
      // A new block inside another block must not disappear. Commit the
      // visible parent text before entering a nested paragraph/list/table
      // cell, then restore the parent after the child closes.
      if(s.collectingDepth && !s.current.runs.empty() &&
         s.items.size()<MAX_BLOCKS) {
        s.items.push_back(std::move(s.current));
      }
      scope.paragraph=true;
      s.collectingDepth=static_cast<int>(s.scopes.size());
      s.current=Paragraph{};
      s.current.align=scope.align;
      s.current.rtl=scope.rtl;
      s.current.heading=n.size()==2 && n[0]=='h';
      // Use original CrossPoint block length resolution, not a Q2 CSS parser.
      s.current.block=BlockStyle::fromCssStyle(
          style,27.0f,CssTextAlign::None,TEXT_WIDTH);
      s.current.block.alignment=scope.align;
      s.current.block.textAlignDefined=true;
      s.current.block.isRtl=scope.rtl;
      s.current.block.directionDefined=style.hasDirection() || !dir.empty();
      scope.pageBreakAfter=s.current.block.pageBreakAfter;
      scope.paragraphBlock=s.current.block;
      scope.heading=s.current.heading;
      if(n=="li") {
        // Preserve list structure instead of silently concatenating item text.
        std::string markerText="- ";
        if(scope.listKind==2) {
          const unsigned number=s.scopes.back().nextItemNumber++;
          markerText=std::to_string(number)+". ";
        }
        s.current.runs.push_back({std::move(markerText),EpdFontFamily::REGULAR});
      }
    }
    s.scopes.push_back(scope);
    if(n=="br" && s.collectingDepth && !scope.hidden) {
      // A hard XHTML line break is not a space: commit the current visual
      // line and continue with a new block, retaining inherited styling.
      BlockStyle retained=s.current.block;
      if(!s.current.runs.empty() && s.items.size()<MAX_BLOCKS)
        s.items.push_back(std::move(s.current));
      s.current=Paragraph{};
      s.current.align=scope.align;
      s.current.rtl=scope.rtl;
      retained.pageBreakBefore=false;
      retained.pageBreakAfter=false;
      retained.marginTop=retained.paddingTop=0;
      s.current.block=retained;
    }
    if((n=="img" || n=="image") && !scope.hidden) {
      const auto uri=attribute(attrs,n=="img"?"src":"href");
      // An EPUB image may live in a sibling directory (../images).
      const std::string resolved=resolveEpubResource(s.chapterDir,uri);
      if(!resolved.empty() && s.items.size()<MAX_BLOCKS) {
        const auto ext=resolved.find_last_of('.');
        std::string extension=ext==std::string::npos?"" :resolved.substr(ext);
        std::transform(extension.begin(),extension.end(),extension.begin(),
                       [](unsigned char c){return static_cast<char>(std::tolower(c));});
        if(extension==".png" || extension==".jpg" || extension==".jpeg") {
          if(s.collectingDepth && !s.current.runs.empty()) {
            s.items.push_back(std::move(s.current));
            s.current=Paragraph{};
            s.current.align=scope.align;
            s.current.rtl=scope.rtl;
          }
          Paragraph image;
          image.imagePath=resolved;
          s.items.push_back(std::move(image));
        }
      }
    }
  }
  static void XMLCALL data(void* user, const XML_Char* bytes, int len) {
    auto& s=*static_cast<HtmlCollector*>(user);
    if(len<=0 || s.limitHit)return;
    const Scope& scope=s.scopes.back();
    if(scope.stylesheet && s.cssText.size()+static_cast<size_t>(len)<=256*1024)
      s.cssText.append(bytes,static_cast<size_t>(len));
    if(!s.collectingDepth || scope.hidden)return;
    if(s.collectedBytes+static_cast<size_t>(len)>MAX_TEXT_BYTES) {
      s.limitHit=true;return;
    }
    auto style=EpdFontFamily::REGULAR;
    if(scope.bold)style=static_cast<EpdFontFamily::Style>(style|EpdFontFamily::BOLD);
    if(scope.italic)style=static_cast<EpdFontFamily::Style>(style|EpdFontFamily::ITALIC);
    if(!s.current.runs.empty() && s.current.runs.back().style==style)
      s.current.runs.back().text.append(bytes,static_cast<size_t>(len));
    else s.current.runs.push_back({std::string(bytes,static_cast<size_t>(len)),style});
    s.collectedBytes+=static_cast<size_t>(len);
  }
  static void XMLCALL end(void* user, const XML_Char* name) {
    auto& s=*static_cast<HtmlCollector*>(user);
    if(s.scopes.size()<=1)return;
    const auto scope=s.scopes.back();
    if(localName(name)=="style" && !s.cssText.empty()) {
      (void)s.addCss(s.cssText);
      s.cssText.clear();
    }
    if(scope.paragraph && static_cast<int>(s.scopes.size())-1==s.collectingDepth) {
      bool nonempty=false;
      for(const auto& run:s.current.runs)
        if(run.text.find_first_not_of(" \t\r\n")!=std::string::npos)nonempty=true;
      if(nonempty && s.items.size()<MAX_BLOCKS)
        s.items.push_back(std::move(s.current));
      else if(s.items.size()>=MAX_BLOCKS)s.limitHit=true;
      if(scope.pageBreakAfter && !s.items.empty())
        s.items.back().block.pageBreakAfter=true;
      s.collectingDepth=0;
      // Resume text after a nested <p>, <li>, <td> or <div> rather than
      // discarding the parent's trailing words.
      for(int d=static_cast<int>(s.scopes.size())-2;d>=1;--d) {
        if(!s.scopes[d].paragraph)continue;
        const auto& parent=s.scopes[d];
        s.collectingDepth=d;
        s.current=Paragraph{};
        s.current.align=parent.align;
        s.current.rtl=parent.rtl;
        s.current.heading=parent.heading;
        s.current.block=parent.paragraphBlock;
        s.current.block.pageBreakBefore=false;
        s.current.block.pageBreakAfter=false;
        s.current.block.marginTop=s.current.block.paddingTop=0;
        break;
      }
    }
    s.scopes.pop_back();
  }
};
// Stream ZIP inflation into Expat. A chapter can be several megabytes; holding
// its entire uncompressed XML alongside the CSS, pagination and image buffers
// needlessly spikes peak RAM on the Q2. This adapter uses CrossPoint's ZipFile
// streaming contract and caps both input and collected text.
class ChapterXmlSink final : public Print {
  XML_Parser parser_;
  size_t seen_=0;
  bool failed_=false;
 public:
  explicit ChapterXmlSink(XML_Parser parser):parser_(parser) {}
  size_t write(uint8_t byte) override {return write(&byte,1);}
  size_t write(const uint8_t* data,size_t len) override {
    if(failed_ || !data || len>MAX_XML_BYTES-seen_ ||
       len>static_cast<size_t>(INT_MAX)) {
      failed_=true;
      return 0;
    }
    if(!len)return 0;
    seen_+=len;
    if(XML_Parse(parser_,reinterpret_cast<const char*>(data),
                 static_cast<int>(len),XML_FALSE)!=XML_STATUS_OK) {
      failed_=true;
      return 0;
    }
    return len;
  }
  bool finish() {
    return !failed_ && seen_!=0 &&
        XML_Parse(parser_,nullptr,0,XML_TRUE)==XML_STATUS_OK;
  }
};

bool parseChapter(ZipFile& archive,const std::string& bookPath,
                  const std::string& name,std::vector<Paragraph>& out) {
  size_t size=0;
  if(!archive.getInflatedFileSize(name.c_str(),&size)||!size||size>MAX_XML_BYTES) {
    std::fprintf(stderr,"CrossPod: unsupported XHTML chapter length: %s (%zu)\n",name.c_str(),size);
    return false;
  }
  XML_Parser parser=XML_ParserCreateNS(nullptr,'|');
  if(!parser)return false;
  HtmlCollector collector(archive,name);
  XML_SetUserData(parser,&collector);
  XML_SetElementHandler(parser,HtmlCollector::start,HtmlCollector::end);
  XML_SetCharacterDataHandler(parser,HtmlCollector::data);
  ChapterXmlSink sink(parser);
  // CSS <link> callbacks also read from the archive. Do not seek the ZIP
  // currently being inflated: use a separate ZipFile cursor for streaming.
  ZipFile chapterStream(bookPath);
  const bool inflated=chapterStream.readFileToStream(name.c_str(),sink,1024);
  const bool ok=inflated && sink.finish();
  if(!ok)std::fprintf(stderr,"CrossPod: XHTML stream/parse error %s: %s\n",
      name.c_str(),XML_ErrorString(XML_GetErrorCode(parser)));
  XML_ParserFree(parser);
  if(!ok||collector.limitHit||collector.items.empty())return false;
  out=std::move(collector.items);
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
struct RenderImage {
  crosspod::PngPreview png;
  int x=0,y=0,width=0,height=0;
};
struct RenderPage {
  std::vector<RenderLine> lines;
  std::vector<RenderImage> images;
};
class Reader {
  std::string bookPath_;
  ZipFile archive_;
  std::unique_ptr<TempCache> temporary_;
  std::unique_ptr<BookMetadataCache> legacyMetadata_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont bodyFont_;
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  std::unique_ptr<SdCardFont> selectedFont_;
  crosspod::ScriptFontFallback scriptFonts_;
  // Keep the complete upstream EPUB parser/Section lifecycle alive for this
  // book. CrossPod's prior collector remains a fail-safe fallback.
  std::shared_ptr<Epub> upstreamEpub_;
  std::unique_ptr<Section> upstreamSection_;
  int upstreamFontId_=0;
  crosspod::Typography typography_{};
  crosspod::ReaderPreferences preferences_{};
  std::string typographyNotice_;
  std::vector<RenderPage> pages_;
  // A bounded 3-page 1-bpp raster cache (~45 KB) avoids repeated SD-page
  // decompression, layout painting and glyph lookups when moving back/forward.
  struct CachedRaster {
    int spine=-1;
    size_t page=0;
    std::vector<uint8_t> pixels;
  };
  std::array<CachedRaster,3> rasterCache_{};
  size_t nextRasterSlot_=0;
  bool lastRenderOk_=true;
  void clearRasterCache() {
    for(auto& item:rasterCache_)item.spine=-1;
  }
  std::vector<crosspod::Highlight> highlights_;
  bool highlightsLoaded_=false;
  std::vector<crosspod::SelectedWord> selectedWords_;
  int selectionAnchor_=-1;
  int selectionFocus_=-1;
  int currentSpine_ = -1;
  size_t currentPage_ = 0;
  bool resumeEnabled_ = false;
  mutable bool positionDirty_=false;
  mutable std::chrono::steady_clock::time_point lastPositionAttempt_{};
  int lineHeight() const { return std::max(22,typography_.size*2); }
  size_t pageCount() const {
    return upstreamSection_?upstreamSection_->pageCount:pages_.size();
  }
  int fontIdForTypography(crosspod::Typography value) const {
    return 100+(value.family==crosspod::Typography::Family::Serif?4:0)+
           (value.size-12)/2;
  }
  ReaderRenderSpec upstreamSpec() const {
    return crosspod::makeReaderRenderSpec(preferences_,upstreamFontId_,
        static_cast<uint16_t>(375),static_cast<uint16_t>(BOTTOM-TOP));
  }
  std::string upstreamCachePath() const {
    struct stat st{};
    (void)::stat(bookPath_.c_str(),&st);
    uint64_t hash=14695981039346656037ull;
    const auto mix=[&](uint64_t value) {
      for(int i=0;i<8;++i){hash^=(value>>(8*i))&255u;hash*=1099511628211ull;}
    };
    for(unsigned char byte:bookPath_){hash^=byte;hash*=1099511628211ull;}
    mix(static_cast<uint64_t>(st.st_size));
    mix(static_cast<uint64_t>(st.st_mtime));
    // CJK font coverage changes line measurement and pagination. Never reuse
    // Section layout cache from a Latin-only build after enabling CJK.
    mix(0x434a4b01ull);
    mix(static_cast<uint64_t>(scriptFonts_.coverageMask()));
    const char* state=std::getenv("CROSSPOD_STATE_DIR");
    struct stat cardDir{};
    const bool cardMounted=::stat("/mnt/mmc/.crosspod",&cardDir)==0 &&
                          S_ISDIR(cardDir.st_mode);
    const std::string base=state&&*state?state:
                           (cardMounted?"/mnt/mmc/.crosspod":"/tmp");
    const std::string cache=base+"/cache";
    Storage.mkdir(cache.c_str());
    char folder[44];
    std::snprintf(folder,sizeof(folder),"book-%016llx",
                  static_cast<unsigned long long>(hash));
    return cache+"/"+folder;
  }
  static bool extractUpstreamImage(void* context,const char* source,const char* dest) {
    auto* self=static_cast<Reader*>(context);
    if(!self || !self->upstreamEpub_ || !source || !dest)return false;
    HalFile out;
    if(!Storage.openFileForWrite("CROSSPOD",dest,out))return false;
    const bool ok=self->upstreamEpub_->readItemContentsToStream(source,out,4096);
    out.close();
    if(!ok)Storage.remove(dest);
    return ok;
  }
  bool useTypography(crosspod::Typography settings) {
    if(!crosspod::validTypography(settings))return false;
    auto next=std::make_unique<SdCardFont>();
    if(!next->load(crosspod::typographyFontPath(settings).c_str()) ||
       !next->getEpdFont()) {
      if(settings.family!=crosspod::Typography::Family::Sans || settings.size!=12)
        return false;
      next.reset();
    }
    // Drop SD glyph registrations BEFORE their owning font handles. Rebuild
    // this size's script-coverage index without loading font bitmaps into RAM.
    scriptFonts_.clear(renderer_);
    renderer_.removeFont(1);
    if(upstreamFontId_)renderer_.removeFont(upstreamFontId_);
    const int nextFontId=fontIdForTypography(settings);
    if(next && next->getEpdFont()) {
      renderer_.insertFont(1,EpdFontFamily(settings.size,next->getEpdFont()));
      renderer_.registerSdCardFont(1,next.get());
      renderer_.insertFont(nextFontId,EpdFontFamily(settings.size,next->getEpdFont()));
      renderer_.registerSdCardFont(nextFontId,next.get());
    } else {
      renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
      renderer_.insertFont(nextFontId,EpdFontFamily(12,&bodyFont_));
    }
    selectedFont_=std::move(next);
    upstreamFontId_=nextFontId;
    scriptFonts_.load(renderer_,settings.size,{1,nextFontId});
    typography_=settings;
    clearRasterCache();
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
    size_t totalImages=0;
    bool pageLimitReached=false;
    for (const auto& p:paragraphs) {
      if(p.block.pageBreakBefore &&
         (!pages_.back().lines.empty() || !pages_.back().images.empty()) &&
         !beginNextPage(y))return false;
      y+=std::clamp<int>(p.block.topInset(),0,48);
      if(y>=BOTTOM && (!pages_.back().lines.empty() ||
         !pages_.back().images.empty()) && !beginNextPage(y))return false;
      if(!p.imagePath.empty()) {
        size_t bytes=0;
        if(!archive_.getInflatedFileSize(p.imagePath.c_str(),&bytes) ||
           bytes==0 || bytes>4u*1024u*1024u)continue;
        std::unique_ptr<uint8_t,decltype(&std::free)> resource(
          archive_.readFileToMemory(p.imagePath.c_str(),&bytes,false),&std::free);
        crosspod::PngPreview png;
        if(!resource)continue;
        const size_t dot=p.imagePath.find_last_of('.');
        std::string extension=dot==std::string::npos?"" :p.imagePath.substr(dot);
        std::transform(extension.begin(),extension.end(),extension.begin(),
                       [](unsigned char c){return static_cast<char>(std::tolower(c));});
        const bool loaded=extension==".png"?
            crosspod::decodePngPreview(resource.get(),bytes,png):
            crosspod::decodeJpegPreview(resource.get(),bytes,png);
        if(!loaded)continue;
        const int width=std::min<int>(TEXT_WIDTH,png.width);
        const int height=std::max<int>(1,static_cast<int>(
          static_cast<int64_t>(png.height)*width/png.width));
        const int fitH=std::min(height,BOTTOM-TOP);
        const int fitW=std::max<int>(1,static_cast<int>(
          static_cast<int64_t>(width)*fitH/height));
        if(y+fitH>BOTTOM && !pages_.back().lines.empty() &&
           !beginNextPage(y))return false;
        if(y+fitH>BOTTOM && !pages_.back().images.empty() &&
           !beginNextPage(y))return false;
        pages_.back().images.push_back({
          std::move(png),LEFT+(TEXT_WIDTH-fitW)/2,y,fitW,fitH});
        ++totalImages;
        y+=fitH+8;
        continue;
      }
      BlockStyle block=p.block;
      block.alignment=p.align;
      block.textAlignDefined=true;
      block.isRtl=p.rtl;
      ParsedText layout(false,false,block,0);
      std::string pending;
      EpdFontFamily::Style pendingStyle=EpdFontFamily::REGULAR;
      // One word may cross inline span boundaries: preserve style runs without
      // converting any UTF-8 bytes or collapsing the whole chapter to ASCII.
      const auto flush=[&]() {
        if(!pending.empty())layout.addWord(std::move(pending),pendingStyle);
        pending.clear();
      };
      for(const auto& run:p.runs) {
        for(const unsigned char ch:run.text) {
          if(ch==' '||ch=='\t'||ch=='\r'||ch=='\n')flush();
          else {
            if(!pending.empty()&&run.style!=pendingStyle)flush();
            pendingStyle=run.style;
            pending.push_back(static_cast<char>(ch));
          }
        }
      }
      flush();
      if (layout.hadDroppedWords()) return false;
      const int inset=std::clamp<int>(block.totalHorizontalInset(),0,96);
      layout.layoutAndExtractLines(renderer_,1,
          static_cast<uint16_t>(TEXT_WIDTH-inset),
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
      y+=std::clamp<int>(p.block.bottomInset(),0,48);
      y+=p.heading?12:8;
      if(p.block.pageBreakAfter &&
         (!pages_.back().lines.empty() || !pages_.back().images.empty()) &&
         !beginNextPage(y))return false;
    }
    if (pages_.size()>1 && pages_.back().lines.empty() &&
        pages_.back().images.empty()) pages_.pop_back();
    if (pages_.empty() || (!totalLines && !totalImages)) return false;
    return true;
  }
  bool loadLegacyMetadata() {
    temporary_=std::make_unique<TempCache>();
    if (temporary_->path().empty()) {
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
    legacyMetadata_=std::make_unique<BookMetadataCache>(temporary_->path());
    if(!legacyMetadata_->beginWrite() || !legacyMetadata_->beginContentOpfPass())return false;
    BookMetadataCache::BookMetadata info;
    {
      ContentOpfParser opf(temporary_->path(),opfDir,size,legacyMetadata_.get(),false);
      if(!opf.setup() || !archive_.readFileToStream(opfPath.c_str(),opf,1024))return false;
      info.title=opf.title;
      info.author=opf.author;
      info.language=opf.language;
      info.coverItemHref=opf.coverItemHref;
      info.textReferenceHref=opf.textReferenceHref;
    }
    return legacyMetadata_->endContentOpfPass() &&
           legacyMetadata_->beginTocPass() && legacyMetadata_->endTocPass() &&
           legacyMetadata_->endWrite() && legacyMetadata_->buildBookBin(bookPath_,info) &&
           legacyMetadata_->load() && legacyMetadata_->getSpineCount()>0;
  }
  BookMetadataCache& metadata() const {
    return upstreamEpub_?upstreamEpub_->getBookMetadataCache():*legacyMetadata_;
  }
 public:
  explicit Reader(std::string path):
      bookPath_(std::move(path)),archive_(bookPath_),
      renderer_(display_),
      bodyFont_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()) {}
  ~Reader() {
    flushPosition();
    ImageBlock::setExtractor(nullptr,nullptr);
  }
  bool open(bool resumeOnOpen=false) {
    (void)crosspod::loadReaderPreferences(preferences_);
    // These are unmodified upstream CrossPoint layout/render sources.
    display_.begin();
    renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer_.begin();
    renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
    // UI font is always fixed; book typography updates must not touch it.
    renderer_.insertFont(2,EpdFontFamily(12,&bodyFont_));
    if (!decompressor_.init()) return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    crosspod::Typography savedType;
    if(!crosspod::loadTypography(savedType)) {
      (void)useTypography(typography_);
    } else if(!useTypography(savedType)) {
      typographyNotice_="Selected font file missing";
      (void)useTypography(typography_);
    }
    std::printf("CrossPod typeface: %s %dpt%s\n",
        crosspod::typographyFamilyName(typography_.family),typography_.size,
        typographyNotice_.empty()?"":" (saved font unavailable; fallback)");
    std::printf("CrossPod font source: %s\n",
        selectedFont_?"original CrossPoint extended cpfont":"built-in emergency font");
    const char* requestedEngine=std::getenv("CROSSPOD_ENGINE");
    if(!requestedEngine || std::strcmp(requestedEngine,"legacy")!=0) {
      auto candidate=std::make_shared<Epub>(bookPath_,upstreamCachePath());
      if(candidate->load(true,false)) {
        upstreamEpub_=std::move(candidate);
        ImageBlock::setExtractor(this,&Reader::extractUpstreamImage);
      } else std::fprintf(stderr,
          "CrossPod: original Section unavailable; using legacy collector\n");
    }
    if(!upstreamEpub_ && !loadLegacyMetadata())return false;
    const auto& info=metadata().coreMetadata;
    std::printf("CrossPod EPUB: %s — %s | %d spine entries\n",
        info.title.c_str(),info.author.c_str(),metadata().getSpineCount());
    // Load the saved chapter first; the old startup rendered spine 0 and
    // immediately discarded it when resume opened a later chapter.
    crosspod::ReadingPosition saved{};
    const bool hasSaved=resumeOnOpen &&
        crosspod::loadReadingPosition(bookPath_,saved) &&
        saved.spine>=0 && saved.spine<metadata().getSpineCount();
    const int firstSpine=hasSaved?saved.spine:0;
    bool loaded=loadSpine(firstSpine,1);
    if(!loaded && firstSpine!=0)loaded=loadSpine(0,1);
    if(!loaded)return false;
    if(resumeOnOpen) {
      resumeEnabled_=true;
      if(hasSaved && currentSpine_==saved.spine && pageCount()>0) {
        // Rebuild a missing page cache only as far as the saved page.
        while(!interrupted && currentSpine_==saved.spine &&
              pageCount()<=saved.page && isBuilding())
          (void)buildNextPages(1);
        if(currentSpine_==saved.spine && pageCount()>0)
          currentPage_=std::min(saved.page,pageCount()-1);
      }
      positionDirty_=false;
      lastPositionAttempt_=std::chrono::steady_clock::now();
    }
    return true;
  }
  void enableResume() {
    resumeEnabled_=true;
    crosspod::ReadingPosition saved;
    if(!crosspod::loadReadingPosition(bookPath_,saved))return;
    if(!loadSpine(saved.spine,1) || currentSpine_!=saved.spine) {
      (void)loadSpine(0,1);
      return;
    }
    currentPage_=std::min(saved.page,pageCount()-1);
    positionDirty_=false;
    lastPositionAttempt_=std::chrono::steady_clock::now();
  }
  void persistPosition(bool force=false) const {
    if(!resumeEnabled_ || currentSpine_<0)return;
    positionDirty_=true;
    const auto now=std::chrono::steady_clock::now();
    if(!force && lastPositionAttempt_.time_since_epoch().count()!=0 &&
       now-lastPositionAttempt_<positionSaveInterval)return;
    // Rate-limit retries too. A removed or read-only card should not cause
    // immediate repeated writes. The dirty position survives a failed attempt.
    lastPositionAttempt_=now;
    if(crosspod::saveReadingPosition(bookPath_,{currentSpine_,currentPage_}))
      positionDirty_=false;
  }
  ReaderClock::time_point nextPositionFlush() const {
    return positionDirty_?lastPositionAttempt_+positionSaveInterval:
                          ReaderClock::time_point::max();
  }
  void flushPosition(bool force=true) const {
    if(positionDirty_)persistPosition(force);
  }
  bool configureTypography(crosspod::Typography requested) {
    if(!crosspod::validTypography(requested))return false;
    if(requested.family==typography_.family && requested.size==typography_.size)return true;
    const auto previous=typography_;
    const int spine=currentSpine_;
    const size_t page=currentPage_;
    const auto offset=upstreamSection_?
      upstreamSection_->getVisibleTextOffsetForPage(
        static_cast<uint16_t>(currentPage_)):std::nullopt;
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
    if(spine>=0 && pageCount()) {
      if(upstreamSection_ && offset) {
        while(upstreamSection_->isBuilding() &&
              !upstreamSection_->buildReachedVisibleTextOffset(*offset)) {
          if(!upstreamSection_->buildSomeMore(2))break;
        }
        currentPage_=upstreamSection_->getPageForVisibleTextOffset(*offset)
            .value_or(static_cast<uint16_t>(std::min(page,pageCount()-1)));
      } else currentPage_=std::min(page,pageCount()-1);
    }
    if(!crosspod::saveTypography(typography_))
      typographyNotice_="Settings not saved to SD";
    else typographyNotice_.clear();
    persistPosition();
    return true;
  }
  crosspod::ShelfDetails shelfDetails() const {
    return {metadata().coreMetadata.title,metadata().coreMetadata.author};
  }
  // Generate a tiny cover once, only after the user has already opened
  // the EPUB. Never inflate cover JPEG/PNG for every bookshelf row or turn.
  bool cacheShelfCover() {
    crosspod::ShelfCover existing;
    if(crosspod::loadShelfCover(bookPath_,existing))return true;
    const std::string& coverPath=metadata().coreMetadata.coverItemHref;
    if(coverPath.empty()||coverPath.size()>1024 ||
       coverPath.front()=='/' || coverPath.find("..")!=std::string::npos)
      return false;
    size_t size=0;
    if(!archive_.getInflatedFileSize(coverPath.c_str(),&size) ||
       size<8 || size>4u*1024u*1024u)return false;
    std::unique_ptr<uint8_t,decltype(&std::free)> bytes(
        archive_.readFileToMemory(coverPath.c_str(),&size,false),&std::free);
    if(!bytes || size<8 || size>4u*1024u*1024u)return false;
    crosspod::PngPreview decoded;
    bool okay=crosspod::decodePngPreview(bytes.get(),size,decoded);
    if(!okay && bytes.get()[0]==0xff && bytes.get()[1]==0xd8)
      okay=crosspod::decodeJpegPreview(bytes.get(),size,decoded);
    if(!okay)return false;
    crosspod::ShelfCover thumbnail;
    return crosspod::makeShelfCover(decoded,thumbnail) &&
           crosspod::saveShelfCover(bookPath_,thumbnail);
  }

  crosspod::Typography typography() const {return typography_;}
  crosspod::ReaderPreferences preferences() const {return preferences_;}
  size_t chapterCount() const {
    if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0)
      return static_cast<size_t>(upstreamEpub_->getTocItemsCount());
    return static_cast<size_t>(metadata().getSpineCount());
  }
  int currentChapter() const {
    if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0 &&
       upstreamSection_) {
      const int item=upstreamSection_->getTocIndexForPage(
        static_cast<int>(currentPage_));
      if(item>=0 && item<upstreamEpub_->getTocItemsCount())return item;
    }
    return currentSpine_;
  }
  size_t currentPage() const {return currentPage_;}
  std::string chapterName(size_t index) {
    if(index>=chapterCount())return {};
    if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0)
      return upstreamEpub_->getTocItem(static_cast<int>(index)).title;
    const auto entry=metadata().getSpineEntry(static_cast<int>(index));
    std::string title=entry.href;
    const size_t slash=title.find_last_of('/');
    if(slash!=std::string::npos)title.erase(0,slash+1);
    const size_t ext=title.find_last_of('.');
    if(ext!=std::string::npos)title.resize(ext);
    return title;
  }
  bool goToChapter(int chapter) {
    if(chapter<0||static_cast<size_t>(chapter)>=chapterCount())return false;
    if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0) {
      const auto item=upstreamEpub_->getTocItem(chapter);
      if(item.spineIndex<0)return false;
      if(!loadSpine(item.spineIndex,1))return false;
      if(upstreamSection_ && !item.anchor.empty()) {
        auto target=upstreamSection_->findAnchor(item.anchor);
        while(!target && upstreamSection_->isBuilding()) {
          if(!upstreamSection_->buildSomeMore(2))break;
          target=upstreamSection_->findAnchor(item.anchor);
        }
        if(target && *target<pageCount())currentPage_=*target;
      }
    } else if(!loadSpine(chapter,1))return false;
    persistPosition();return true;
  }
  // Sync uses the original CrossPoint XPath/visible-text mapper. Never
  // translate a remote page number directly: each device repaginates EPUBs.
  std::string syncDocumentHash() const {
    return crosspod::koReaderPartialMd5(bookPath_);
  }
  bool pushReadingSync() {
    const auto config=crosspod::loadConnectedConfig();
    if(!config.syncConfigured() || !upstreamEpub_ || !upstreamSection_) {
      typographyNotice_="Enable sync in connected.conf";
      return false;
    }
    const auto hash=syncDocumentHash();
    if(hash.empty()) {
      typographyNotice_="Cannot hash EPUB";
      return false;
    }
    CrossPointPosition position{};
    position.spineIndex=currentSpine_;
    position.pageNumber=static_cast<int>(currentPage_);
    position.totalPages=static_cast<int>(std::max<size_t>(1,pageCount()));
    if(auto offset=upstreamSection_->getVisibleTextOffsetForPage(
         static_cast<uint16_t>(currentPage_))) {
      position.visibleTextOffset=*offset;
      position.hasVisibleTextOffset=true;
    }
    const auto mapped=ProgressMapper::toSavedProgress(upstreamEpub_,position);
    if(mapped.xpath.empty()) {
      typographyNotice_="Cannot map text position";
      return false;
    }
    crosspod::RemoteProgress remote{};
    std::string error;
    if(!crosspod::pullKoreaderProgress(config,hash,remote,error)) {
      typographyNotice_="Sync check: "+error;
      return false;
    }
    // Never silently overwrite a newer/further location from another
    // device. The reader's own local position stays intact on conflicts.
    if(remote.exists &&
       remote.percentage>mapped.percentage+0.002f &&
       remote.progress!=mapped.xpath) {
      typographyNotice_="Remote ahead. Pull first.";
      return false;
    }
    const crosspod::RemoteProgress local{
        mapped.xpath,std::clamp(mapped.percentage,0.f,1.f),true};
    if(!crosspod::pushKoreaderProgress(config,hash,local,error)) {
      typographyNotice_="Sync upload: "+error;
      return false;
    }
    typographyNotice_="Progress uploaded";
    return true;
  }
  bool pullReadingSync() {
    const auto config=crosspod::loadConnectedConfig();
    if(!config.syncConfigured() || !upstreamEpub_ || !upstreamSection_) {
      typographyNotice_="Enable sync in connected.conf";
      return false;
    }
    const auto hash=syncDocumentHash();
    if(hash.empty()) {
      typographyNotice_="Cannot hash EPUB";
      return false;
    }
    crosspod::RemoteProgress remote{};
    std::string error;
    if(!crosspod::pullKoreaderProgress(config,hash,remote,error)) {
      typographyNotice_="Sync download: "+error;
      return false;
    }
    if(!remote.exists) {
      typographyNotice_="No remote progress";
      return false;
    }
    SavedProgressPosition saved{remote.progress,remote.percentage};
    const auto mapped=ProgressMapper::toCrossPoint(
        upstreamEpub_,saved,renderer_,currentSpine_,
        static_cast<int>(pageCount()));
    // Do not apply rough percentage estimates as though they were exact
    // KOReader positions. A font or screen size change shifts page numbers.
    if(!mapped.hasResolvedSpineIndex ||
       (!mapped.hasVisibleTextOffset && !mapped.hasMappedPage) ||
       mapped.spineIndex<0 ||
       mapped.spineIndex>=metadata().getSpineCount()) {
      typographyNotice_="Remote text location unsupported";
      return false;
    }
    if(!goToBookmark({mapped.spineIndex,
           static_cast<size_t>(std::max(0,mapped.pageNumber)),
           mapped.hasVisibleTextOffset?mapped.visibleTextOffset:0u})) {
      typographyNotice_="Could not open synced position";
      return false;
    }
    clearRasterCache();
    persistPosition(true);
    typographyNotice_="Remote position applied";
    return true;
  }
  void dumpToc() {
    if(!upstreamEpub_)return;
    const int count=upstreamEpub_->getTocItemsCount();
    std::printf("CrossPod TOC entries: %d\n",count);
    for(int i=0;i<count;++i) {
      const auto t=upstreamEpub_->getTocItem(i);
      std::printf("toc=%d spine=%d depth=%u title=%s anchor=%s\n",
          i,t.spineIndex,t.level,t.title.c_str(),t.anchor.c_str());
    }
  }
  // The original CrossPoint Page already serializes clickable link boxes
  // and EPUB footnote references. These APIs only adapt Q2 input to them.
  std::vector<std::string> pageLinks() {
    std::vector<std::string> hrefs;
    if(!upstreamSection_)return hrefs;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return hrefs;
    for(const auto& link:page->links) {
      if(!link.href[0])continue;
      const std::string href=link.href;
      if(std::find(hrefs.begin(),hrefs.end(),href)==hrefs.end())
        hrefs.push_back(href);
    }
    for(const auto& note:page->footnotes) {
      if(!note.href[0])continue;
      const std::string href=note.href;
      if(std::find(hrefs.begin(),hrefs.end(),href)==hrefs.end())
        hrefs.push_back(href);
    }
    return hrefs;
  }
  std::vector<crosspod::Bookmark> jumpHistory_;
  bool followLink(const std::string& href) {
    if(currentSpine_<0 || !upstreamSection_ || !upstreamEpub_)return false;
    const auto current=metadata().getSpineEntry(currentSpine_).href;
    const auto destination=crosspod::resolveInternalLink(current,href);
    if(!destination.valid)return false;
    int target=-1;
    for(int i=0;i<metadata().getSpineCount();++i) {
      if(metadata().getSpineEntry(i).href==destination.path) {
        target=i;break;
      }
    }
    if(target<0)return false;
    if(jumpHistory_.size()>=16)jumpHistory_.erase(jumpHistory_.begin());
    const auto original=crosspod::Bookmark{
        currentSpine_,currentPage_,upstreamSection_->getVisibleTextOffsetForPage(
          static_cast<uint16_t>(currentPage_)).value_or(0)};
    if(target!=currentSpine_ && !loadSpine(target,1))return false;
    if(!destination.anchor.empty()) {
      if(!upstreamSection_)return false;
      auto page=upstreamSection_->findAnchor(destination.anchor);
      // Don't stall reading for unbounded chapters or unresolved links.
      for(int steps=0;!page && upstreamSection_->isBuilding() && steps<128;++steps) {
        if(!upstreamSection_->buildSomeMore(2))break;
        page=upstreamSection_->findAnchor(destination.anchor);
      }
      if(!page)return false;
      currentPage_=*page;
    } else currentPage_=0;
    jumpHistory_.push_back(original);
    clearRasterCache();
    persistPosition();
    return true;
  }
  bool goBackFromLink() {
    if(jumpHistory_.empty())return false;
    const auto mark=jumpHistory_.back();
    if(!goToBookmark(mark))return false;
    jumpHistory_.pop_back();
    clearRasterCache();
    return true;
  }
  bool hasLinkHistory() const {return !jumpHistory_.empty();}
  bool isFootnoteLink(const std::string& href) {
    if(!upstreamSection_)return false;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return false;
    for(const auto& note:page->footnotes)
      if(href==note.href)
        return !footnotePreview(href).empty();
    return false;
  }
  std::string touchedLink(int touchX,int touchY) {
    if(!upstreamSection_ || touchY<TOP || touchY>=BOTTOM)return {};
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return {};
    for(const auto& link:page->links)
      if(link.href[0] &&
         link.contains(touchX-preferences_.margin,touchY-TOP,5,20))
        return link.href;
    return {};
  }
  std::string footnotePreview(const std::string& href) {
    if(currentSpine_<0)return {};
    return crosspod::loadFootnotePreview(archive_,
      metadata().getSpineEntry(currentSpine_).href,href);
  }
  void showFootnotePopup(const std::string& href) {
    std::string note=footnotePreview(href);
    render();
    renderer_.fillRect(10,58,355,250,true);
    renderer_.fillRect(12,60,351,246,false);
    renderer_.drawText(2,20,70,"Footnote");
    if(note.empty())note="Preview unavailable. Center: open note";
    size_t cursor=0;
    for(int line=0;line<9 && cursor<note.size();++line) {
      size_t end=std::min(note.size(),cursor+43);
      if(end<note.size()) {
        const size_t space=note.rfind(' ',end);
        if(space!=std::string::npos && space>cursor)end=space;
      }
      renderer_.drawText(2,20,101+line*19,
          clippedLabel(note.substr(cursor,end-cursor),42).c_str());
      cursor=end;
      while(cursor<note.size() && note[cursor]==' ')++cursor;
    }
    drawMenuText(20,284,"Back: close   Center: jump",1);
    display_.displayBuffer();
  }
  bool followTouchedLink(int touchX,int touchY) {
    if(!upstreamSection_ || touchY<TOP || touchY>=BOTTOM)return false;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return false;
    for(const auto& link:page->links) {
      if(!link.href[0])continue;
      if(link.contains(touchX-preferences_.margin,touchY-TOP,5,20))
        return followLink(link.href);
    }
    return false;
  }
  void showLinksMenu(int selected) {
    render();
    renderer_.fillRect(10,38,355,258,true);
    renderer_.fillRect(12,40,351,254,false);
    drawMenuText(22,48,"Links and footnotes");
    const auto links=pageLinks();
    if(links.empty())drawMenuText(22,118,"No links on this page",1);
    for(int row=0;row<8;++row) {
      const int index=(selected/8)*8+row;
      if(index>=static_cast<int>(links.size()))break;
      const int y=76+row*24;
      const auto label=clippedLabel(
          std::to_string(index+1)+". "+links[index],48);
      drawMenuText(23,y,label.c_str());
      if(index==selected)invertMenuRect(18,y-3,337,21);
    }
    drawMenuText(22,278,"Wheel: select  Center: follow");
    display_.displayBuffer();
  }
  const std::vector<crosspod::Highlight>& highlights() {
    if(!highlightsLoaded_) {
      highlights_=crosspod::loadHighlights(bookPath_);
      highlightsLoaded_=true;
    }
    return highlights_;
  }
  bool beginTextSelection() {
    if(!upstreamSection_)return false;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return false;
    selectedWords_=crosspod::selectableWords(
        *page,renderer_,upstreamFontId_,preferences_.margin,TOP);
    selectionAnchor_=selectedWords_.empty()?-1:0;
    selectionFocus_=selectionAnchor_;
    return selectionAnchor_>=0;
  }
  bool selectTouchWord(int x,int y,bool extend) {
    if(!extend || selectedWords_.empty())
      if(!beginTextSelection())return false;
    const int picked=crosspod::hitSelectableWord(selectedWords_,x,y);
    if(picked<0)return false;
    if(!extend)selectionAnchor_=picked;
    selectionFocus_=picked;
    return true;
  }
  bool stepSelection(int direction,bool extend=false) {
    if(selectedWords_.empty() && !beginTextSelection())return false;
    selectionFocus_=std::clamp(selectionFocus_+direction,0,
                              static_cast<int>(selectedWords_.size())-1);
    if(!extend)selectionAnchor_=selectionFocus_;
    return true;
  }
  void discardSelection() {
    selectedWords_.clear();
    selectionAnchor_=-1;selectionFocus_=-1;
  }
  bool selectionRange(uint32_t& begin,uint32_t& end) const {
    if(selectionAnchor_<0||selectionFocus_<0 ||
       selectionAnchor_>=static_cast<int>(selectedWords_.size()) ||
       selectionFocus_>=static_cast<int>(selectedWords_.size()))return false;
    begin=UINT32_MAX;end=0;
    const int lo=std::min(selectionAnchor_,selectionFocus_);
    const int hi=std::max(selectionAnchor_,selectionFocus_);
    for(int i=lo;i<=hi;++i) {
      begin=std::min(begin,selectedWords_[i].begin);
      end=std::max(end,selectedWords_[i].end);
    }
    return begin<end;
  }
  std::string selectedText() const {
    if(selectionAnchor_<0||selectionFocus_<0)return {};
    const int lo=std::min(selectionAnchor_,selectionFocus_);
    const int hi=std::max(selectionAnchor_,selectionFocus_);
    std::string text;
    for(int i=lo;i<=hi && text.size()<400;++i) {
      if(!text.empty())text+=' ';
      text+=selectedWords_[i].text;
    }
    return text.substr(0,400);
  }
  bool saveSelectionHighlight() {
    uint32_t start=0,end=0;
    if(!selectionRange(start,end) ||
       !crosspod::addHighlight(bookPath_,{
         static_cast<uint32_t>(currentSpine_),start,end}))return false;
    highlightsLoaded_=false;
    return true;
  }
  bool eraseSelectionHighlight() {
    uint32_t start=0,end=0;
    if(!selectionRange(start,end) ||
       !crosspod::removeHighlightAt(bookPath_,
         static_cast<uint32_t>(currentSpine_),start))return false;
    highlightsLoaded_=false;
    return true;
  }
  void paintHighlightOverlay() {
    if(!upstreamSection_ || currentSpine_<0)return;
    const auto& highlights=this->highlights();
    if(highlights.empty() && selectionAnchor_<0)return;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return;
    const auto words=crosspod::selectableWords(
        *page,renderer_,upstreamFontId_,preferences_.margin,TOP);
    uint32_t start=0,end=0;
    const bool active=selectionRange(start,end);
    for(const auto& word:words) {
      bool marked=active &&
          crosspod::overlapsVisibleWord(word,start,end);
      if(!marked)
        for(const auto& h:highlights)
          if(h.spine==static_cast<uint32_t>(currentSpine_) &&
             crosspod::overlapsVisibleWord(word,h.begin,h.end)) {
            marked=true;break;
          }
      if(marked)invertMenuRect(word.x-1,word.y+2,word.width+2,
                              std::max(8,word.height-7));
    }
  }
  void showSelectionPreview(bool extending=false) {
    render();
    renderer_.fillRect(10,254,355,47,true);
    renderer_.fillRect(12,256,351,43,false);
    drawMenuText(20,265,extending?
        "Wheel: extend  Center: actions":
        "Wheel: word   Center: start");
    display_.displayBuffer();
  }
  void showTextSelectionMenu(int option) {
    render();
    renderer_.fillRect(10,207,355,96,true);
    renderer_.fillRect(12,209,351,92,false);
    renderer_.drawText(2,20,215,"Select text");
    const auto text=clippedLabel(selectedText(),44);
    renderer_.drawText(2,20,235,text.c_str());
    const char* choices[]={"Highlight","Define","Remove","Cancel"};
    for(int i=0;i<4;++i) {
      const int x=20+i*85;
      drawMenuText(x,270,choices[i],1);
      if(option==i)invertMenuRect(x-3,265,83,23);
    }
    display_.displayBuffer();
  }
  void showDictionaryDefinition() {
    const auto selected=selectedText();
    const auto def=crosspod::lookupDefinition(selected);
    render();
    renderer_.fillRect(10,90,355,195,true);
    renderer_.fillRect(12,92,351,191,false);
    renderer_.drawText(2,20,103,"Offline dictionary");
    renderer_.drawText(2,20,133,clippedLabel(selected,32).c_str());
    const std::string description=def.empty()?
      "No match in dictionary.tsv":def;
    // Break across a few display rows without modifying UTF-8 source text.
    size_t cursor=0;
    for(int line=0;line<5 && cursor<description.size();++line) {
      size_t end=std::min(description.size(),cursor+43);
      if(end<description.size()) {
        const size_t space=description.rfind(' ',end);
        if(space!=std::string::npos && space>cursor)end=space;
      }
      renderer_.drawText(2,20,158+line*20,
          clippedLabel(description.substr(cursor,end-cursor),42).c_str());
      cursor=end;
      while(cursor<description.size() && description[cursor]==' ')++cursor;
    }
    drawMenuText(20,271,"Back: selected text",1);
    display_.displayBuffer();
  }
  bool rememberPage() {
    const uint32_t offset=upstreamSection_?
      upstreamSection_->getVisibleTextOffsetForPage(
        static_cast<uint16_t>(currentPage_)).value_or(0):0;
    const bool saved=crosspod::addBookmark(bookPath_,
      {currentSpine_,currentPage_,offset});
    typographyNotice_=saved?"Bookmark saved":"Cannot save bookmark";
    return saved;
  }
  std::vector<crosspod::Bookmark> bookmarks() const {
    return crosspod::loadBookmarks(bookPath_);
  }
  bool goToBookmark(const crosspod::Bookmark& mark) {
    // Bookmark chapter numbers are OPF spine indices, not TOC entry ordinals.
    if(mark.spine<0 || !loadSpine(mark.spine,1))return false;
    if(upstreamSection_ && mark.textOffset) {
      while(upstreamSection_->isBuilding() &&
           !upstreamSection_->buildReachedVisibleTextOffset(mark.textOffset)) {
        if(!upstreamSection_->buildSomeMore(2))break;
      }
      currentPage_=upstreamSection_->getPageForVisibleTextOffset(
        mark.textOffset).value_or(static_cast<uint16_t>(
          std::min(mark.page,pageCount()-1)));
    } else currentPage_=std::min(mark.page,pageCount()-1);
    persistPosition();
    return true;
  }
  void showNavigationMenu(bool showingBookmarks,int selected) {
    render();
    renderer_.fillRect(9,38,357,255,true);
    renderer_.fillRect(11,40,353,251,false);
    renderer_.drawText(1,23,48,showingBookmarks?"Bookmarks":"Chapters");
    const auto marks=showingBookmarks?bookmarks():
      std::vector<crosspod::Bookmark>{};
    const size_t count=showingBookmarks?marks.size():chapterCount();
    if(!count)renderer_.drawText(1,25,127,"No bookmarks yet");
    const int start=selected<0?0:(selected/7)*7;
    for(int row=0;row<7 && start+row<static_cast<int>(count);++row) {
      const int item=start+row;
      const int y=81+row*25;
      const bool active=item==selected;
      if(active)renderer_.fillRect(17,y-3,341,22,true);
      std::string label;
      if(showingBookmarks) {
        const auto& mark=marks[item];
        label="Ch "+std::to_string(mark.spine+1)+
          " - Page "+std::to_string(mark.page+1);
      } else label=std::to_string(item+1)+". "+chapterName(item);
      label=clippedLabel(label,34);
      renderer_.drawText(1,24,y,label.c_str(),!active);
    }
    renderer_.drawText(1,23,265,"Wheel: select  Center: go");
    renderer_.drawText(1,23,286,"Back: reading settings");
    display_.displayBuffer();
  }

  bool adjustPreference(int option) {
    crosspod::ReaderPreferences next=preferences_;
    switch(option) {
      case 2: next.linePercent=next.linePercent==100?120:
          (next.linePercent==120?140:100);break;
      case 3: next.margin=next.margin==14?22:
          (next.margin==22?30:14);break;
      case 4: next.alignment=1-next.alignment;break;
      case 5: next.hyphenation=!next.hyphenation;break;
      case 6: next.embeddedStyle=!next.embeddedStyle;break;
      case 7: next.nightMode=!next.nightMode;break;
      default:return false;
    }
    if(!crosspod::validReaderPreferences(next))return false;
    if(option==7) {
      // Inversion is a framebuffer setting; no new Section layout and no
      // SD cache invalidation are necessary.
      preferences_=next;
      clearRasterCache();
      const bool saved=crosspod::saveReaderPreferences(preferences_);
      typographyNotice_=saved?"":"Reading settings not saved";
      return true;
    }
    const auto previous=preferences_;
    const int spine=currentSpine_;
    const size_t priorPage=currentPage_;
    auto offset=upstreamSection_?
      upstreamSection_->getVisibleTextOffsetForPage(
        static_cast<uint16_t>(currentPage_)):std::nullopt;
    preferences_=next;
    clearRasterCache();
    if(spine>=0 && (!loadSpine(spine,1)||currentSpine_!=spine)) {
      preferences_=previous;
      clearRasterCache();
      (void)loadSpine(spine,1);
      currentPage_=std::min(priorPage,pageCount()-1);
      typographyNotice_="Could not update page layout";
      return false;
    }
    // Preserve content position when a completed Section gives an exact
    // visible-text offset; do not assume the old page number is stable after
    // changing margins or line spacing.
    if(upstreamSection_ && offset) {
      while(upstreamSection_->isBuilding() &&
            !upstreamSection_->buildReachedVisibleTextOffset(*offset)) {
        if(!upstreamSection_->buildSomeMore(2))break;
      }
      auto nextPage=upstreamSection_->getPageForVisibleTextOffset(*offset);
      currentPage_=nextPage.value_or(
        static_cast<uint16_t>(std::min(priorPage,pageCount()-1)));
    } else if(pageCount())currentPage_=std::min(priorPage,pageCount()-1);
    if(!crosspod::saveReaderPreferences(preferences_))
      typographyNotice_="Reading settings not saved";
    else typographyNotice_.clear();
    persistPosition();
    return true;
  }
  // Built-in 12pt font ID 2 stays constant when the reader font changes.
  // Scale it for compact menus without loading different SD-card glyph sizes.
  // Crisp fixed 5x7 pixel glyphs; no bilinear resampling or SD font I/O.
  // 2x integer scale makes 10x14 glyphs and fixed 12px character advance.
  // This typography is isolated from the configurable EPUB reading font.
  void drawMenuText(int x,int top,const char* str,int scale=2) {
    static constexpr uint8_t glyphs[95][5]={
    {0x00,0x00,0x00,0x00,0x00}, // 32
    {0x00,0x00,0x5f,0x00,0x00}, // 33
    {0x00,0x07,0x00,0x07,0x00}, // 34
    {0x12,0x3f,0x12,0x3f,0x12}, // 35
    {0x12,0x15,0x1f,0x05,0x08}, // 36
    {0x13,0x0b,0x04,0x1a,0x19}, // 37
    {0x36,0x49,0x55,0x2a,0x50}, // 38
    {0x00,0x04,0x03,0x00,0x00}, // 39
    {0x00,0x1c,0x22,0x41,0x00}, // 40
    {0x00,0x41,0x22,0x1c,0x00}, // 41
    {0x2a,0x1c,0x3e,0x1c,0x2a}, // 42
    {0x08,0x08,0x3e,0x08,0x08}, // 43
    {0x00,0x40,0x30,0x10,0x00}, // 44
    {0x08,0x08,0x08,0x08,0x08}, // 45
    {0x00,0x60,0x60,0x00,0x00}, // 46
    {0x40,0x30,0x08,0x06,0x01}, // 47
    {0x3e,0x51,0x4d,0x43,0x3e}, // 48
    {0x00,0x42,0x7f,0x40,0x00}, // 49
    {0x42,0x61,0x51,0x49,0x46}, // 50
    {0x41,0x49,0x49,0x49,0x36}, // 51
    {0x18,0x14,0x12,0x7f,0x10}, // 52
    {0x4f,0x49,0x49,0x49,0x31}, // 53
    {0x3e,0x49,0x49,0x49,0x30}, // 54
    {0x01,0x71,0x09,0x05,0x03}, // 55
    {0x36,0x49,0x49,0x49,0x36}, // 56
    {0x06,0x49,0x49,0x49,0x3e}, // 57
    {0x00,0x36,0x36,0x00,0x00}, // 58
    {0x00,0x56,0x36,0x00,0x00}, // 59
    {0x08,0x14,0x22,0x41,0x00}, // 60
    {0x0a,0x0a,0x0a,0x0a,0x0a}, // 61
    {0x00,0x41,0x22,0x14,0x08}, // 62
    {0x02,0x01,0x51,0x09,0x06}, // 63
    {0x3e,0x41,0x5d,0x55,0x5e}, // 64
    {0x7e,0x09,0x09,0x09,0x7e}, // 65
    {0x7f,0x49,0x49,0x49,0x36}, // 66
    {0x3e,0x41,0x41,0x41,0x41}, // 67
    {0x7f,0x41,0x41,0x41,0x3e}, // 68
    {0x7f,0x49,0x49,0x49,0x41}, // 69
    {0x7f,0x09,0x09,0x09,0x01}, // 70
    {0x3e,0x41,0x49,0x49,0x79}, // 71
    {0x7f,0x08,0x08,0x08,0x7f}, // 72
    {0x00,0x41,0x7f,0x41,0x00}, // 73
    {0x30,0x40,0x41,0x3f,0x01}, // 74
    {0x7f,0x08,0x14,0x22,0x41}, // 75
    {0x7f,0x40,0x40,0x40,0x40}, // 76
    {0x7f,0x02,0x0c,0x02,0x7f}, // 77
    {0x7f,0x02,0x04,0x08,0x7f}, // 78
    {0x3e,0x41,0x41,0x41,0x3e}, // 79
    {0x7f,0x09,0x09,0x09,0x06}, // 80
    {0x3e,0x41,0x51,0x21,0x5e}, // 81
    {0x7f,0x09,0x19,0x29,0x46}, // 82
    {0x46,0x49,0x49,0x49,0x31}, // 83
    {0x01,0x01,0x7f,0x01,0x01}, // 84
    {0x3f,0x40,0x40,0x40,0x3f}, // 85
    {0x1f,0x20,0x40,0x20,0x1f}, // 86
    {0x3f,0x40,0x38,0x40,0x3f}, // 87
    {0x63,0x14,0x08,0x14,0x63}, // 88
    {0x03,0x04,0x78,0x04,0x03}, // 89
    {0x61,0x51,0x49,0x45,0x43}, // 90
    {0x00,0x7f,0x41,0x41,0x00}, // 91
    {0x01,0x06,0x08,0x30,0x40}, // 92
    {0x00,0x41,0x41,0x7f,0x00}, // 93
    {0x04,0x02,0x01,0x02,0x04}, // 94
    {0x40,0x40,0x40,0x40,0x40}, // 95
    {0x00,0x01,0x02,0x04,0x00}, // 96
    {0x20,0x54,0x54,0x54,0x78}, // 97
    {0x7f,0x44,0x44,0x44,0x38}, // 98
    {0x38,0x44,0x44,0x44,0x44}, // 99
    {0x38,0x44,0x44,0x44,0x7f}, // 100
    {0x38,0x54,0x54,0x54,0x58}, // 101
    {0x08,0x7e,0x09,0x01,0x02}, // 102
    {0x0c,0x52,0x52,0x52,0x3e}, // 103
    {0x7f,0x04,0x04,0x04,0x78}, // 104
    {0x00,0x44,0x7d,0x40,0x00}, // 105
    {0x20,0x40,0x44,0x3d,0x00}, // 106
    {0x7f,0x10,0x28,0x44,0x00}, // 107
    {0x00,0x41,0x7f,0x40,0x00}, // 108
    {0x7c,0x04,0x78,0x04,0x78}, // 109
    {0x7c,0x04,0x04,0x04,0x78}, // 110
    {0x38,0x44,0x44,0x44,0x38}, // 111
    {0x7c,0x14,0x14,0x14,0x08}, // 112
    {0x08,0x14,0x14,0x14,0x7c}, // 113
    {0x7c,0x08,0x04,0x04,0x08}, // 114
    {0x48,0x54,0x54,0x54,0x24}, // 115
    {0x04,0x3f,0x44,0x40,0x20}, // 116
    {0x3c,0x40,0x40,0x20,0x7c}, // 117
    {0x1c,0x20,0x40,0x20,0x1c}, // 118
    {0x3c,0x40,0x30,0x40,0x3c}, // 119
    {0x44,0x28,0x10,0x28,0x44}, // 120
    {0x0c,0x50,0x50,0x50,0x3c}, // 121
    {0x44,0x64,0x54,0x4c,0x44}, // 122
    {0x00,0x08,0x36,0x41,0x00}, // 123
    {0x00,0x00,0x7f,0x00,0x00}, // 124
    {0x00,0x41,0x36,0x08,0x00}, // 125
    {0x04,0x02,0x04,0x04,0x02}, // 126
    };
    for(const unsigned char* p=
          reinterpret_cast<const unsigned char*>(str);*p;++p) {
      const unsigned cp=*p>=32 && *p<=126?*p:static_cast<unsigned>('?');
      if(x+6*scale>352)break;
      const uint8_t* glyph=glyphs[cp-32];
      for(int gx=0;gx<5;++gx)
        for(int gy=0;gy<7;++gy)
          if(glyph[gx] & (1u<<gy))
            renderer_.fillRect(x+gx*scale,top+gy*scale,scale,scale,true);
      x+=6*scale;
    }
  }
  void invertMenuRect(int x,int y,int w,int h) {
    uint8_t* fb=display_.getFrameBuffer();
    if(!fb)return;
    for(int py=std::max(0,y);py<std::min(320,y+h);++py)
      for(int px=std::max(0,x);px<std::min(375,x+w);++px)
        fb[py*HalDisplay::DISPLAY_WIDTH_BYTES+px/8]^=
          static_cast<uint8_t>(0x80u>>(px&7));
  }
  void showTypographyMenu(int selected) {
    renderer_.clearScreen(0xff); // Do not reload and render the EPUB on each wheel step.
    // The Q2 has a 360x320 logical viewport. Keep the heading and footer
    // outside a bounded list; never paint eleven oversized rows at once.
    constexpr int kItems=16;
    constexpr int kRows=9;
    constexpr int kRowTop=73;
    constexpr int kRowHeight=21;
    const int first=std::min(std::max(0,selected-kRows+1),kItems-kRows);
    renderer_.fillRect(10,37,355,258,true);
    renderer_.fillRect(12,39,351,254,false);
    drawMenuText(22,47,"Reading settings");
    renderer_.fillRect(18,66,337,1,true);
    const char* labels[]={"Font","Size","Line spacing","Margins","Alignment",
        "Hyphenation","Book CSS","Night mode","Add bookmark","Bookmarks","Chapters",
        "Page links","Return link","Select text","Sync: upload","Sync: download"};
    const std::string values[]={
      crosspod::typographyFamilyName(typography_.family),
      std::to_string(typography_.size)+"pt",
      std::to_string(preferences_.linePercent)+"%",
      std::to_string(preferences_.margin)+"px",
      preferences_.alignment?"Justify":"Book default",
      preferences_.hyphenation?"On":"Off",
      preferences_.embeddedStyle?"On":"Off",
      preferences_.nightMode?"On":"Off",
      "Save page","Saved pages","Browse book","Footnotes / links",
      hasLinkHistory()?"Go back":"No previous link","Highlight / define",
      "Save position","Use remote position"
    };
    for(int row=0;row<kRows && first+row<kItems;++row) {
      const int item=first+row;
      const int y=kRowTop+row*kRowHeight;
      const bool active=selected==item;
      // Invert the completed row, including the glyph pixels, after drawing.
      // Separate columns prevent long values from colliding with labels.
      const std::string label=clippedLabel(labels[item],12);
      const std::string value=clippedLabel(values[item],12);
      drawMenuText(24,y+3,label.c_str());
      drawMenuText(192,y+3,value.c_str());
      if(active)invertMenuRect(18,y,337,kRowHeight);
    }
    renderer_.fillRect(18,267,337,1,true);
    const std::string position=std::to_string(selected+1)+"/"+
      std::to_string(kItems)+"  Wheel: scroll  Center: select";
    drawMenuText(23,277,clippedLabel(position,48).c_str(),1);
    if(!typographyNotice_.empty()) {
      const std::string notice=clippedLabel(typographyNotice_,46);
      drawMenuText(23,292,clippedLabel(notice,48).c_str(),1);
    }
    display_.displayBuffer();
  }
  bool loadSpine(int index,int direction) {
    const int oldSpine=currentSpine_;
    const size_t oldPage=currentPage_;
    auto oldPages=std::move(pages_);
    auto oldSection=std::move(upstreamSection_);
    const int n=metadata().getSpineCount();
    while(index>=0 && index<n) {
      if(upstreamEpub_ && index<upstreamEpub_->getSpineItemsCount()) {
        auto section=std::make_unique<Section>(upstreamEpub_,index,renderer_);
        const ReaderRenderSpec spec=upstreamSpec();
        // Restore complete/partial pages immediately. On a cache miss
        // build only until the first page is available; the event loop will
        // grow the original Section in bounded increments between inputs.
        const bool cached=section->loadSectionFile(spec);
        bool usable=cached && section->pageCount>0;
        if(!cached && section->startBuild(spec)) {
          while(section->isBuilding() && section->pageCount==0) {
            if(!section->buildSomeMore(1))break;
          }
          usable=section->pageCount>0;
        } else if(cached && section->isPartial() && section->startBuild(spec)) {
          // Partial's cached pages remain immediately readable while the
          // next incremental pass rebuilds its page LUT in the background.
          usable=true;
        }
        if(usable) {
          // Reading backwards across a spine requires its *actual* last
          // page, never the partial watermark.
          if(direction<0 && section->isBuilding()) {
            if(!section->buildSomeMore(0))usable=false;
          }
        }
        if(usable) {
          upstreamSection_=std::move(section);
          currentSpine_=index;
          currentPage_=direction<0?upstreamSection_->pageCount-1:0;
          pages_.clear();
          std::printf("CrossPod original Section: spine %d/%d, %zu page(s)%s\n",
              index+1,n,pageCount(),
              upstreamSection_->isBuilding()?" (incremental)":"");
          return true;
        }
        std::fprintf(stderr,
           "CrossPod: original Section could not lay out spine %d; falling back\n",index+1);
      }
      const auto entry=metadata().getSpineEntry(index);
      std::vector<Paragraph> paragraphs;
      if(parseChapter(archive_,bookPath_,entry.href,paragraphs) &&
         paginate(paragraphs)) {
        upstreamSection_.reset();
        currentSpine_=index;
        currentPage_=direction<0?pages_.size()-1:0;
        std::printf("CrossPod legacy fallback: spine %d/%d, %zu page(s)\n",
            index+1,n,pages_.size());
        return true;
      }
      index+=direction;
    }
    pages_=std::move(oldPages);
    upstreamSection_=std::move(oldSection);
    currentPage_=oldPage;
    currentSpine_=oldSpine;
    return false;
  }
  bool buildNextPages(int maxPages=1) {
    if(!upstreamSection_ || !upstreamSection_->isBuilding())return false;
    const size_t before=pageCount();
    if(!upstreamSection_->buildSomeMore(maxPages)) {
      std::fprintf(stderr,"CrossPod: incremental Section failed; preserving book via fallback\n");
      const int spine=currentSpine_;
      const size_t page=currentPage_;
      if(!loadLegacyMetadata())return false;
      upstreamSection_.reset();
      upstreamEpub_.reset();
      if(loadSpine(spine,1)) {
        currentPage_=std::min(page,pageCount()-1);
        persistPosition();
      }
      return true;
    }
    if(!upstreamSection_->isBuilding())
      std::printf("CrossPod Section completed: %zu pages\n",pageCount());
    return pageCount()!=before;
  }
  bool isBuilding() const {return upstreamSection_ && upstreamSection_->isBuilding();}
  // Keep only a short page-ahead window warm. For long EPUB chapters the
  // old continuous 90ms parser loop kept reading/writing SD and using CPU
  // even when the reader spent minutes on the same page.
  bool needsPagePrefetch() const {
    return isBuilding() && pageCount()<=currentPage_+5;
  }
  size_t estimatedPages() const {
    return upstreamSection_?upstreamSection_->estimatedTotalPages():pageCount();
  }
  bool move(int direction) {
    if(direction>0 && currentPage_+1>=pageCount() && isBuilding()) {
      (void)buildNextPages(2);
      // No new page available yet: remain in this chapter until the
      // incremental parser finishes. Never silently skip unread content.
      if(isBuilding() && currentPage_+1>=pageCount())return false;
    }
    if (direction>0 && currentPage_+1<pageCount()) { ++currentPage_; persistPosition(); return true; }
    if (direction<0 && currentPage_>0) { --currentPage_; persistPosition(); return true; }
    const bool changed=loadSpine(currentSpine_+direction,direction);
    if(changed)persistPosition();
    return changed;
  }
  void applyNightMode() {
    if(!preferences_.nightMode)return;
    auto* buffer=display_.getFrameBuffer();
    if(!buffer)return;
    for(size_t i=0;i<display_.getBufferSize();++i)
      buffer[i]=static_cast<uint8_t>(~buffer[i]);
  }
  void drawProgress() {
    const int totalChapters=metadata().getSpineCount();
    if(totalChapters<=0 || currentSpine_<0)return;
    const size_t localCount=std::max<size_t>(1,estimatedPages());
    const float local=static_cast<float>(currentPage_+1)/localCount;
    const float progress=std::clamp(
      (static_cast<float>(currentSpine_)+local)/totalChapters,0.0f,1.0f);
    const int filled=static_cast<int>(progress*347.0f);
    renderer_.drawLine(14,314,361,314,true);
    if(filled>0)renderer_.fillRect(14,312,filled,3,true);
  }
  void cacheRenderedPage() {
    const uint8_t* framebuffer=display_.getFrameBuffer();
    if(!framebuffer || currentSpine_<0)return;
    auto& slot=rasterCache_[nextRasterSlot_++%rasterCache_.size()];
    slot.spine=currentSpine_;
    slot.page=currentPage_;
    slot.pixels.assign(framebuffer,framebuffer+HalDisplay::BUFFER_SIZE);
  }
  void render() {
    uint8_t* framebuffer=display_.getFrameBuffer();
    constexpr size_t kBytes=HalDisplay::BUFFER_SIZE;
    if(framebuffer) {
      for(const auto& cached:rasterCache_)
        if(cached.spine==currentSpine_ && cached.page==currentPage_ &&
           cached.pixels.size()==kBytes) {
          std::memcpy(framebuffer,cached.pixels.data(),kBytes);
          lastRenderOk_=true;
          drawProgress();
          applyNightMode();
          paintHighlightOverlay();
          return;
        }
    }
    if(upstreamSection_) {
      auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
      if(!page) {
        lastRenderOk_=false;
        // An incomplete or transiently unreadable Section page is NOT a
        // successful blank page. Preserve the previous framebuffer and retry
        // on the next draw instead of permanently caching an empty raster.
        std::fprintf(stderr,
            "CrossPod: page %zu in spine %d unavailable; refusing blank cache\n",
            currentPage_,currentSpine_);
        return;
      }
      renderer_.clearScreen(0xff);
      page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
      // If a long session evicted a glyph cache in an unusable state, make
      // exactly one fresh attempt rather than recording an all-white raster
      // in the three-page cache (which persisted until the app restarted).
      if(!pageHasVisibleInk()) {
        fontCache_.clearCache();
        renderer_.clearScreen(0xff);
        page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
      }
      lastRenderOk_=pageHasVisibleInk();
      if(lastRenderOk_)cacheRenderedPage();
      else {
        // Direct bitmap menu glyphs work even when the SD-book font has
        // failed; leave a visible diagnostic instead of a silent white page.
        drawMenuText(20,140,"Page text unavailable",1);
        std::fprintf(stderr,"CrossPod: no body ink after glyph retry at spine %d page %zu\n",
                     currentSpine_,currentPage_);
      }
      drawProgress();
      applyNightMode();
      paintHighlightOverlay();
      return;
    }
    if(currentPage_>=pages_.size()) {
      std::fputs("CrossPod: legacy page unavailable; preserving current framebuffer\n",stderr);
      return;
    }
    renderer_.clearScreen(0xff);
    const auto& page=pages_[currentPage_];
    for (const auto& line:page.lines)
      line.block->render(renderer_,1,preferences_.margin,line.y);
    // 4x4 ordered Bayer halftone; preserve image detail on Q2's 1-bit surface.
    static constexpr uint8_t bayer[4][4]={
      {0,8,2,10},{12,4,14,6},{3,11,1,9},{15,7,13,5}};
    for(const auto& image:page.images) {
      for(int dy=0;dy<image.height;++dy) {
        const size_t sy=static_cast<size_t>(dy)*image.png.height/image.height;
        for(int dx=0;dx<image.width;++dx) {
          const size_t sx=static_cast<size_t>(dx)*image.png.width/image.width;
          const unsigned gray=image.png.gray[sy*image.png.width+sx];
          const unsigned threshold=static_cast<unsigned>(bayer[dy&3][dx&3])*16u+8u;
          if(gray<threshold)renderer_.drawPixel(image.x+dx+(preferences_.margin-14),image.y+dy,true);
        }
      }
    }
    cacheRenderedPage();
    drawProgress();
    applyNightMode();
    paintHighlightOverlay();
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
  bool lastRenderSucceeded() const { return lastRenderOk_; }
  bool pageHasVisibleInk() const {
    const uint8_t* pixels=display_.getFrameBuffer();
    if(!pixels)return false;
    // Ignore progress and UI footer: only count actual rendered book area.
    for(int y=TOP;y<BOTTOM;++y)
      for(int xByte=2;xByte<44;++xByte)
        if(pixels[y*HalDisplay::DISPLAY_WIDTH_BYTES+xByte]!=0xff)
          return true;
    return false;
  }
  size_t imageCount() {
    if(upstreamSection_) {
      size_t result=0;
      for(int i=0;i<upstreamSection_->pageCount;++i) {
        auto page=upstreamSection_->loadPage(i);
        if(page)for(const auto& element:page->elements)
          if(element->getTag()==TAG_PageImage)++result;
      }
      return result;
    }
    size_t n=0;for(const auto& page:pages_)n+=page.images.size();
    return n;
  }
  void report() const {
    std::printf("CrossPod validated %s: page %zu/%zu, spine %d/%d\n",
        bookPath_.c_str(),currentPage_+1,pageCount(),
        currentSpine_+1,metadata().getSpineCount());
  }
};


// Q2 Pod card-app launcher: the card executable receives no book-path argv.
// The Books shelf is intentionally independent of the stock reader and only
// discovers regular, non-hidden EPUB files from the SD card (one subdir deep).
using crosspod::ShelfEntry;
using crosspod::scanCardBooks;
std::string clippedLabel(const std::string& label,size_t maxCodepoints) {
  return crosspod::clippedUiLabel(label,maxCodepoints);
}

class CardShelf {
  const std::vector<ShelfEntry>& books_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont font_;
  crosspod::ScriptFontFallback scriptFonts_;
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  size_t selected_ = 0;  // index within filtered browsing rows
  size_t current_ = 0;   // index within the unfiltered library
  std::vector<size_t> visible_;
  std::string searchQuery_;
  bool searchEditing_=false;
  size_t searchKey_=0;
  crosspod::ShelfSortMode sortMode_=crosspod::ShelfSortMode::Title;
  std::vector<crosspod::ShelfDetails> cachedDetails_;
  std::vector<bool> detailsChecked_;
  crosspod::ShelfCover currentCover_{};
  bool currentCoverChecked_=false;
  bool currentCoverAvailable_=false;
  bool browsing_ = false;
  bool onlineMode_=false;
  std::string onlineUrl_;
  std::string onlineTitle_;
  std::vector<crosspod::OpdsEntry> onlineEntries_;
  std::vector<std::string> onlineHistory_;
  size_t onlineSelection_=0;
  int homeSelection_ = 0;
  std::string notice_;
  static constexpr int TOP_Y=80, ROW_H=38, VISIBLE_ROWS=5;
  static constexpr int HOME_ONLINE_X=252;

 public:
  explicit CardShelf(const std::vector<ShelfEntry>& books,size_t selection=0,
                     size_t current=0,
                     crosspod::ShelfSortMode sortMode=crosspod::ShelfSortMode::Title):
      books_(books),renderer_(display_),font_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()) {
    sortMode_=sortMode;
    cachedDetails_.resize(books_.size());
    detailsChecked_.resize(books_.size(),false);
    refreshVisible();
    if(!books_.empty()) {
      selected_=std::min(selection,books_.size()-1);
      current_=std::min(current,books_.size()-1);
    }
  }
  bool open() {
    display_.begin();
    renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer_.begin();
    renderer_.insertFont(1,EpdFontFamily(12,&font_));
    // All scripts in EPUB filenames use the same SD font fallback machinery.
    scriptFonts_.load(renderer_,12,{1});
    if(!decompressor_.init())return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    return true;
  }
  size_t selected() const {
    return browsing_ && !visible_.empty()?visible_[selected_]:current_;
  }
  bool empty() const {return books_.empty();}
  bool hasMatches() const {return !visible_.empty();}
  bool browsing() const {return browsing_;}
  bool browseChosen() const {return !browsing_ && (homeSelection_==1 || books_.empty());}
  bool sortChosen() const {return !browsing_ && homeSelection_==2;}
  bool onlineChosen() const {return !browsing_ && homeSelection_==3;}
  bool onlineMode() const {return onlineMode_;}
  bool searching() const {return browsing_ && searchEditing_;}
  bool enterOnline() {
    const auto config=crosspod::loadConnectedConfig();
    if(!config.configured()) {
      notice_="Set .crosspod/connected.conf";
      return false;
    }
    return loadOnlinePage(config.opdsUrl,false);
  }
  bool loadOnlinePage(const std::string& url,bool saveHistory) {
    auto feed=crosspod::fetchOpdsFeed(url);
    if(!feed.error.empty()) {
      notice_=feed.error;
      return false;
    }
    if(saveHistory && onlineHistory_.size()<12)
      onlineHistory_.push_back(onlineUrl_);
    onlineUrl_=url;
    onlineTitle_=feed.title.empty()?"Connected library":feed.title;
    onlineEntries_=std::move(feed.entries);
    onlineSelection_=0;
    onlineMode_=true;
    browsing_=true;
    searchEditing_=false;
    notice_.clear();
    return true;
  }
  void returnFromOnline() {
    if(onlineHistory_.empty()) {
      onlineMode_=false;onlineEntries_.clear();goHome();return;
    }
    const auto previous=onlineHistory_.back();
    onlineHistory_.pop_back();
    // A failed network fetch must not strand the user. Back twice exits.
    auto feed=crosspod::fetchOpdsFeed(previous);
    if(!feed.error.empty()) {
      onlineMode_=false;onlineEntries_.clear();goHome();return;
    }
    onlineUrl_=previous;
    onlineTitle_=feed.title;
    onlineEntries_=std::move(feed.entries);
    onlineSelection_=0;
    notice_.clear();
  }
  bool onlineHasSelection() const {return onlineSelection_<onlineEntries_.size();}
  crosspod::OpdsEntry onlineEntry() const {
    return onlineHasSelection()?onlineEntries_[onlineSelection_]:
      crosspod::OpdsEntry{};
  }
  void onlineTouch(int x,int y) {
    if(y<TOP_Y || y>=TOP_Y+VISIBLE_ROWS*ROW_H)return;
    const size_t start=(onlineSelection_/VISIBLE_ROWS)*VISIBLE_ROWS;
    const size_t selected=start+static_cast<size_t>((y-TOP_Y)/ROW_H);
    if(selected<onlineEntries_.size())onlineSelection_=selected;
  }
  void enterBrowser() {browsing_=true;searchEditing_=false;notice_.clear();}
  void goHome() {
    browsing_=false;onlineMode_=false;searchEditing_=false;homeSelection_=0;
    searchQuery_.clear();refreshVisible();notice_.clear();
  }
  void enterSearch() {
    browsing_=true;searchEditing_=true;searchKey_=0;
    notice_.clear();
  }
  void endSearch() {searchEditing_=false;notice_.clear();}
  static constexpr size_t SEARCH_KEY_COUNT=40;
  static constexpr const char* SEARCH_LETTERS=
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
  bool applySearchChoice() {
    if(searchKey_<36) {
      if(searchQuery_.size()<24)searchQuery_.push_back(SEARCH_LETTERS[searchKey_]);
    } else if(searchKey_==36) {
      if(searchQuery_.size()<24)searchQuery_+=' ';
    } else if(searchKey_==37) {
      if(!searchQuery_.empty())searchQuery_.pop_back();
    } else if(searchKey_==38) {
      endSearch();return true;
    } else {
      searchQuery_.clear();
    }
    refreshVisible();
    selected_=0;
    return true;
  }
  void refreshVisible() {
    visible_.clear();
    for(size_t i=0;i<books_.size();++i) {
      if(searchQuery_.empty()) {visible_.push_back(i);continue;}
      const auto& info=details(i);
      if(crosspod::shelfSearchMatches(
          searchQuery_,books_[i].label,info.title,info.author))
        visible_.push_back(i);
    }
    if(visible_.empty() || selected_>=visible_.size())selected_=0;
  }
  const std::string& bookPath() const {return books_[selected()].path;}
  bool change(int direction) {
    notice_.clear();
    if(onlineMode_) {
      if(onlineEntries_.empty())return false;
      const size_t before=onlineSelection_;
      if(direction>0 && onlineSelection_+1<onlineEntries_.size())
        ++onlineSelection_;
      else if(direction<0 && onlineSelection_>0)--onlineSelection_;
      return before!=onlineSelection_;
    }
    if(searchEditing_) {
      const auto before=searchKey_;
      searchKey_=static_cast<size_t>((
        static_cast<int>(searchKey_)+direction+
        static_cast<int>(SEARCH_KEY_COUNT))%static_cast<int>(SEARCH_KEY_COUNT));
      return searchKey_!=before;
    }
    if(!browsing_) {
      const int next=std::clamp(homeSelection_+direction,0,3);
      if(next==homeSelection_)return false;
      homeSelection_=next;
      return true;
    }
    if(visible_.empty())return false;
    const auto before=selected_;
    if(direction>0 && selected_+1<visible_.size())++selected_;
    else if(direction<0 && selected_>0)--selected_;
    return selected_!=before;
  }
  bool tap(int x,int y) {
    if(x<0||x>=375)return false;
    if(onlineMode_) {
      if(y<73) {returnFromOnline();return true;}
      onlineTouch(x,y);return true;
    }
    if(searchEditing_) {
      if(y>=269){endSearch();return true;}
      if(y<88||y>=250||x<12||x>=362)return false;
      const size_t column=static_cast<size_t>((x-12)/50);
      const size_t row=static_cast<size_t>((y-88)/27);
      const size_t option=row*7+column;
      if(option>=SEARCH_KEY_COUNT)return false;
      searchKey_=option;
      return applySearchChoice();
    }
    if(!browsing_) {
      if(y>=158&&y<195)homeSelection_=0;
      else if(y>=204&&y<241)homeSelection_=1;
      else if(y>=244&&y<267)homeSelection_=x<HOME_ONLINE_X?2:3;
      else return false;
      return true;
    }
    if(y>=39 && y<74) {
      if(x>=190)return enterOnline();
      enterSearch();return true;
    }
    if(visible_.empty()||y<TOP_Y||y>=TOP_Y+VISIBLE_ROWS*ROW_H)return false;
    const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
    const size_t index=start+static_cast<size_t>((y-TOP_Y)/ROW_H);
    if(index>=visible_.size())return false;
    selected_=index;
    return true;
  }
  const crosspod::ShelfDetails& details(size_t index) {
    if(!detailsChecked_[index]) {
      detailsChecked_[index]=true;
      (void)crosspod::loadShelfDetails(books_[index].path,cachedDetails_[index]);
    }
    return cachedDetails_[index];
  }
  void drawBookCover(int x,int y,size_t index) {
    if(!currentCoverChecked_) {
      currentCoverAvailable_=crosspod::loadShelfCover(books_[index].path,currentCover_);
      currentCoverChecked_=true;
    }
    renderer_.drawLine(x-1,y-1,x+crosspod::ShelfCover::width,y-1,true);
    renderer_.drawLine(x-1,y-1,x-1,y+crosspod::ShelfCover::height,true);
    renderer_.drawLine(x+crosspod::ShelfCover::width,y-1,
                       x+crosspod::ShelfCover::width,y+crosspod::ShelfCover::height,true);
    renderer_.drawLine(x-1,y+crosspod::ShelfCover::height,
                       x+crosspod::ShelfCover::width,y+crosspod::ShelfCover::height,true);
    if(!currentCoverAvailable_) {
      renderer_.drawLine(x+7,y+12,x+39,y+12,true);
      renderer_.drawLine(x+7,y+18,x+39,y+18,true);
      return;
    }
    for(int py=0;py<crosspod::ShelfCover::height;++py)
      for(int px=0;px<crosspod::ShelfCover::width;++px)
        if(currentCover_.ink[py*crosspod::ShelfCover::stride+px/8] &
           (0x80u>>(px&7)))
          renderer_.drawPixel(x+px,y+py,true);
  }
  void onIndexedBook(const std::string& path) {
    // A new cache file may have appeared since this shelf object was made.
    // Refresh only that book's lazy metadata (and current cover, if any).
    for(size_t i=0;i<books_.size();++i) {
      if(books_[i].path!=path)continue;
      detailsChecked_[i]=false;
      cachedDetails_[i]={};
      if(i==current_) {
        currentCoverChecked_=false;
        currentCoverAvailable_=false;
      }
      if(!searchQuery_.empty())refreshVisible();
      break;
    }
  }
  void setNotice(std::string notice) {notice_=std::move(notice);}
  // Draw compact ASCII interface labels using CrossPoint's existing
  // glyph scaler. Reader/body fonts and multilingual fallbacks are unchanged.
  void drawCompactLabel(int x,int baseline,const char* label,int scale=192) {
    for(const unsigned char* cp=
          reinterpret_cast<const unsigned char*>(label);*cp;++cp) {
      if(*cp>=0x80)continue; // these fixed UI labels are ASCII
      x+=renderer_.drawScaledCodepoint(1,*cp,
          EpdFontFamily::REGULAR,x,baseline,scale);
    }
  }
  void drawRowLabel(int x,int y,int height,const char* label,bool black=true) {
    const int textY=y+(height-renderer_.getLineHeight(1))/2;
    renderer_.drawText(1,x,textY,label,black);
  }
  void paint() {
    renderer_.clearScreen(0xff);
    // Fixed title and footer are outside both the home and file-list regions.
    // Invert the header raster so its text stays white on the dark title bar.
    drawRowLabel(16,0,39,"CrossPod");
    uint8_t* fb=display_.getFrameBuffer();
    for(int y=0;y<39;++y)
      for(int byte=0;byte<HalDisplay::DISPLAY_WIDTH_BYTES;++byte)
        fb[y*HalDisplay::DISPLAY_WIDTH_BYTES+byte]^=0xff;
    if(onlineMode_) {
      renderer_.drawText(1,16,49,
          clippedLabel(onlineTitle_.empty()?"Online library":onlineTitle_,35).c_str());
      renderer_.drawLine(15,72,359,72,true);
      if(onlineEntries_.empty()) {
        renderer_.drawText(1,20,120,"No EPUBs or folders");
        drawCompactLabel(20,159,"Back: leave catalog",190);
      }
      const size_t start=(onlineSelection_/VISIBLE_ROWS)*VISIBLE_ROWS;
      for(size_t row=0;row<VISIBLE_ROWS && start+row<onlineEntries_.size();++row) {
        const size_t index=start+row;
        const int y=TOP_Y+static_cast<int>(row)*ROW_H;
        const bool active=index==onlineSelection_;
        if(active)renderer_.fillRect(12,y,350,36,true);
        else renderer_.drawLine(15,y+36,359,y+36,true);
        const auto& entry=onlineEntries_[index];
        const std::string prefix=entry.kind==crosspod::OpdsEntry::Kind::Epub?
          "[EPUB] ":"[DIR] ";
        renderer_.drawText(1,20,y+3,
          clippedLabel(prefix+entry.title,31).c_str(),!active);
        if(!entry.author.empty())
          renderer_.drawText(1,20,y+21,
            clippedLabel(entry.author,32).c_str(),!active);
      }
    } else if(!browsing_) {
      drawRowLabel(16,39,35,"Home");
      renderer_.drawLine(15,74,359,74,true);
      // Real EPUB cover thumb is decoded once when leaving the book;
      // shelf startup only reads a 360-byte 1-bit cached image.
      if(!books_.empty())drawBookCover(18,86,current_);
      else {
        renderer_.drawLine(17,85,66,85,true);
        renderer_.drawLine(17,85,17,146,true);
        renderer_.drawLine(66,85,66,146,true);
        renderer_.drawLine(17,146,66,146,true);
      }
      drawRowLabel(82,85,20,"Current book");
      if(books_.empty())
        drawRowLabel(82,107,20,"No books yet");
      else {
        const auto& info=details(current_);
        const auto& title=info.title.empty()?books_[current_].label:info.title;
        drawRowLabel(82,106,20,renderer_.truncatedText(1,title.c_str(),277).c_str());
        if(!info.author.empty())
          drawRowLabel(82,127,20,renderer_.truncatedText(1,info.author.c_str(),277).c_str());
      }
      renderer_.drawLine(15,149,359,149,true);
      if(!books_.empty()) {
        if(homeSelection_==0)renderer_.fillRect(12,158,350,37,true);
        drawRowLabel(22,158,37,"Continue reading",homeSelection_!=0);
      }
      if(homeSelection_==1 || books_.empty())
        renderer_.fillRect(12,204,350,37,true);
      drawRowLabel(22,204,37,"Browse files",
          homeSelection_!=1 && !books_.empty());
      std::string sortText="Sort: ";
      sortText+=crosspod::shelfSortLabel(sortMode_);
      if(homeSelection_==2)renderer_.fillRect(12,244,HOME_ONLINE_X-22,23,true);
      if(homeSelection_==3)renderer_.fillRect(HOME_ONLINE_X,244,362-HOME_ONLINE_X,23,true);
      drawRowLabel(22,244,23,
          renderer_.truncatedText(1,sortText.c_str(),HOME_ONLINE_X-42).c_str(),
          homeSelection_!=2);
      drawRowLabel(HOME_ONLINE_X+10,244,23,"Online",homeSelection_!=3);
    } else if(searchEditing_) {
      drawCompactLabel(16,59,"Search library",190);
      const std::string query="Find: "+searchQuery_;
      drawCompactLabel(16,77,clippedLabel(query,34).c_str(),175);
      for(size_t key=0;key<SEARCH_KEY_COUNT;++key) {
        const int column=static_cast<int>(key%7);
        const int row=static_cast<int>(key/7);
        const int x=12+column*50, y=88+row*27;
        const bool active=key==searchKey_;
        if(active)renderer_.fillRect(x,y,48,25,true);
        else renderer_.drawLine(x,y+24,x+47,y+24,true);
        char single[2]={0,0};
        const char* label=nullptr;
        if(key<36) {single[0]=SEARCH_LETTERS[key];label=single;}
        else if(key==36)label="SP";
        else if(key==37)label="DEL";
        else if(key==38)label="DONE";
        else label="CLR";
        renderer_.drawText(1,x+3,y+2,label,!active);
      }
      char count[72];
      std::snprintf(count,sizeof(count),"%zu matching / %zu books",
                    visible_.size(),books_.size());
      drawCompactLabel(16,262,count,165);
    } else {
      char count[64];
      std::snprintf(count,sizeof(count),"Books: %zu/%zu",visible_.size(),books_.size());
      drawCompactLabel(16,61,count);
      drawCompactLabel(212,61,"Search | Online",133);
      renderer_.drawLine(15,72,359,72,true);
      if(visible_.empty()) {
        renderer_.drawText(1,16,102,books_.empty()?"No EPUB books found":"No matching books");
        renderer_.drawText(1,16,144,
            books_.empty()?"Copy EPUBs to /Books":"Tap title to edit search");
      } else {
        const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
        for(size_t row=0;row<VISIBLE_ROWS && start+row<visible_.size();++row) {
          const size_t filtered=start+row;
          const size_t index=visible_[filtered];
          const int y=TOP_Y+static_cast<int>(row)*ROW_H;
          const bool active=filtered==selected_;
          if(active)renderer_.fillRect(12,y,350,36,true);
          else renderer_.drawLine(15,y+36,359,y+36,true);
          const auto& info=details(index);
          const std::string label=clippedLabel(
              info.title.empty()?books_[index].label:info.title,26);
          renderer_.drawText(1,22,y+3,label.c_str(),!active);
          if(!info.author.empty())
            renderer_.drawText(1,22,y+21,
                clippedLabel(info.author,29).c_str(),!active);
        }
      }
    }
    // A single footer line; previous "Wheel: choose" at y=261 overlapped
    // the dark footer and became partly invisible on the actual LCD.
    renderer_.fillRect(0,269,375,51,true);
    const std::string footer=!notice_.empty()
      ?clippedLabel(notice_,24)
      :onlineMode_?"OK: open/get   Back: prev":
       searchEditing_?"Wheel: letters   OK: add   Back: results":
        browsing_?"OK: open   Back: home":"OK: open   Back: exit";
    drawRowLabel(22,269,51,footer.c_str(),false);
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
  if(!installExitHandlers())return 5;
  SessionProfile profile;
  std::vector<ShelfEntry> books=scanCardBooks(bookDir);
  if(interrupted)return 0;
  auto recent=crosspod::loadRecentBooks();
  auto sortMode=crosspod::ShelfSortMode::Title;
  crosspod::sortShelfBooks(books,sortMode,recent);
  crosspod::Q2SocketInput input;
  if(!input.open()) {
    std::fputs("CrossPod card: cannot bind /tmp/q2-crosspod.input.sock\n",stderr);
    return 5;
  }
  size_t selection=crosspod::lastOpenedIndex(books,recent);
  std::unique_ptr<CardShelf> shelf(new CardShelf(books,selection,selection,sortMode));
  if(!shelf->open())return 4;
  if(interrupted)return 0;
  shelf->present();
  crosspod::Q2Controls controls(loadWheelSensitivity());
  std::unique_ptr<Reader> reader;
  bool typeMenu=false;
  bool navigationMenu=false;
  bool linkMenu=false;
  bool selectionChoosing=false;
  bool selectionExtending=false;
  bool selectionActions=false;
  bool dictionaryOverlay=false;
  bool footnoteOverlay=false;
  std::string footnoteHref;
  int selectionAction=0;
  bool touchPressed=false;
  int touchStartX=0,touchStartY=0;
  std::chrono::steady_clock::time_point touchStartTime{};
  bool showingBookmarks=false;
  int navigationSelection=0;
  int typeSelection=0;
  auto nextBuildTick=std::chrono::steady_clock::now();
  size_t nextShelfIndex=0;
  auto nextShelfIndexTick=std::chrono::steady_clock::now()+
      std::chrono::milliseconds(800);
  while(!interrupted) {
    if(reader)reader->flushPosition(false);
    crosspod::PhysicalEvent event{};
    if(!input.receive(event)) {
      const auto now=std::chrono::steady_clock::now();
      // Work only while idle on the bookshelf, not when reading or during
      // input handling. One bounded OPF/thumbnail attempt per idle tick;
      // never parse chapter pages or write an index on every wheel turn.
      if(shelf && nextShelfIndex<books.size() && now>=nextShelfIndexTick) {
        const std::string& book=books[nextShelfIndex++].path;
        const auto result=crosspod::indexShelfBook(book);
        if(result.updated) {
          shelf->onIndexedBook(book);
          shelf->present();
        }
        nextShelfIndexTick=std::chrono::steady_clock::now()+
            std::chrono::milliseconds(800);
      }
      const bool canPrefetch=reader && !typeMenu && !navigationMenu && !linkMenu &&
          !selectionChoosing && !selectionActions && !dictionaryOverlay &&
          !footnoteOverlay && reader->needsPagePrefetch();
      if(canPrefetch && now>=nextBuildTick) {
        // Build only while idle, with no framebuffer refresh.
        (void)reader->buildNextPages(2);
        nextBuildTick=ReaderClock::now()+std::chrono::milliseconds(150);
      }
      auto deadline=reader?reader->nextPositionFlush():ReaderClock::time_point::max();
      if(shelf && nextShelfIndex<books.size())deadline=std::min(deadline,nextShelfIndexTick);
      if(canPrefetch && reader->needsPagePrefetch())deadline=std::min(deadline,nextBuildTick);
      if(waitForReaderInput(input,deadline)<0) {
        std::fputs("CrossPod: input socket poll failed\n",stderr);
        return 5;
      }
      continue;
    }
    if(interrupted)break;
    const auto action=controls.translate(event).action;
    nextShelfIndexTick=std::chrono::steady_clock::now()+
        std::chrono::milliseconds(800);
    nextBuildTick=std::chrono::steady_clock::now()+
                  std::chrono::milliseconds(300);
    if(reader) {
      if(footnoteOverlay) {
        if(action==crosspod::LogicalAction::Back ||
           (event.kind==crosspod::PhysicalKind::TouchUp && event.x<187)) {
          footnoteOverlay=false;
          reader->present();
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          footnoteOverlay=false;
          const bool jumped=reader->followLink(footnoteHref);
          if(jumped) {
            linkMenu=false;typeMenu=false;reader->present();
          } else reader->present();
        }
        continue;
      }
      if(dictionaryOverlay) {
        if(action==crosspod::LogicalAction::Back ||
           action==crosspod::LogicalAction::Confirm ||
           event.kind==crosspod::PhysicalKind::TouchUp) {
          dictionaryOverlay=false;
          selectionActions=true;
          reader->showTextSelectionMenu(selectionAction);
        }
        continue;
      }
      if(selectionActions) {
        if(action==crosspod::LogicalAction::Back) {
          selectionActions=false;reader->discardSelection();reader->present();
        } else if(action==crosspod::LogicalAction::Next ||
                  action==crosspod::LogicalAction::Previous) {
          selectionAction=(selectionAction+
              (action==crosspod::LogicalAction::Next?1:3))%4;
          reader->showTextSelectionMenu(selectionAction);
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          if(event.kind==crosspod::PhysicalKind::TouchUp) {
            if(event.y<260||event.y>=302)continue;
            selectionAction=std::clamp((event.x-20)/85,0,3);
          }
          if(selectionAction==1) {
            selectionActions=false;dictionaryOverlay=true;
            reader->showDictionaryDefinition();
          } else {
            if(selectionAction==0)(void)reader->saveSelectionHighlight();
            if(selectionAction==2)(void)reader->eraseSelectionHighlight();
            selectionActions=false;reader->discardSelection();reader->present();
          }
        }
        continue;
      }
      if(selectionChoosing) {
        if(action==crosspod::LogicalAction::Back) {
          selectionChoosing=false;selectionExtending=false;
          reader->discardSelection();reader->present();
        } else if(action==crosspod::LogicalAction::Next ||
                  action==crosspod::LogicalAction::Previous) {
          (void)reader->stepSelection(
              action==crosspod::LogicalAction::Next?1:-1,
              selectionExtending);
          reader->showSelectionPreview(selectionExtending);
        } else if(action==crosspod::LogicalAction::Confirm) {
          if(!selectionExtending) {
            selectionExtending=true;
            reader->showSelectionPreview(true);
          } else {
            selectionChoosing=false;selectionExtending=false;
            selectionActions=true;selectionAction=0;
            reader->showTextSelectionMenu(0);
          }
        } else if(event.kind==crosspod::PhysicalKind::TouchUp) {
          if(reader->selectTouchWord(event.x,event.y,selectionExtending))
            reader->showSelectionPreview(selectionExtending);
        }
        continue;
      }
      if(linkMenu) {
        const auto links=reader->pageLinks();
        const int count=static_cast<int>(links.size());
        if(action==crosspod::LogicalAction::Back) {
          linkMenu=false;
          reader->showTypographyMenu(typeSelection);
        } else if(action==crosspod::LogicalAction::Next ||
                  action==crosspod::LogicalAction::Previous) {
          if(count)navigationSelection=std::clamp(
              navigationSelection+
              (action==crosspod::LogicalAction::Next?1:-1),0,count-1);
          reader->showLinksMenu(navigationSelection);
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          if(event.kind==crosspod::PhysicalKind::TouchUp)
            navigationSelection=std::clamp(
                (navigationSelection/8)*8+(event.y-76)/24,
                0,std::max(0,count-1));
          if(count && navigationSelection<count &&
             reader->isFootnoteLink(links[navigationSelection])) {
            footnoteHref=links[navigationSelection];
            footnoteOverlay=true;
            reader->showFootnotePopup(footnoteHref);
          } else {
            const bool jumped=count && navigationSelection<count &&
                reader->followLink(links[navigationSelection]);
            if(jumped) {
              linkMenu=false;typeMenu=false;reader->present();
            } else reader->showLinksMenu(navigationSelection);
          }
        }
        continue;
      }
      if(navigationMenu) {
        const int count=static_cast<int>(showingBookmarks?
          reader->bookmarks().size():reader->chapterCount());
        if(action==crosspod::LogicalAction::Back) {
          navigationMenu=false;
          reader->showTypographyMenu(typeSelection);
        } else if(action==crosspod::LogicalAction::Next ||
                  action==crosspod::LogicalAction::Previous) {
          if(count)navigationSelection=std::clamp(
            navigationSelection+(action==crosspod::LogicalAction::Next?1:-1),
            0,count-1);
          reader->showNavigationMenu(showingBookmarks,navigationSelection);
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          if(event.kind==crosspod::PhysicalKind::TouchUp)
            navigationSelection=std::clamp(
              (navigationSelection/7)*7+(event.y-81)/25,
              0,std::max(0,count-1));
          bool changed=false;
          if(count && showingBookmarks) {
            const auto marks=reader->bookmarks();
            if(navigationSelection<static_cast<int>(marks.size()))
              changed=reader->goToBookmark(marks[navigationSelection]);
          } else if(count)changed=reader->goToChapter(navigationSelection);
          if(changed) {
            navigationMenu=false;
            typeMenu=false;
            reader->present();
          } else reader->showNavigationMenu(showingBookmarks,navigationSelection);
        }
        continue;
      }
      if(typeMenu) {
        if(action==crosspod::LogicalAction::Back) {
          typeMenu=false;
          reader->present();
        } else if(action==crosspod::LogicalAction::Previous ||
                  action==crosspod::LogicalAction::Next) {
          typeSelection=(typeSelection+
              (action==crosspod::LogicalAction::Next?1:15))%16;
          reader->showTypographyMenu(typeSelection);
        } else if(action==crosspod::LogicalAction::Confirm ||
                  event.kind==crosspod::PhysicalKind::TouchUp) {
          if(event.kind==crosspod::PhysicalKind::TouchUp)
            typeSelection=std::clamp(
                std::max(0,typeSelection-8)+(event.y-73)/21,0,15);
          if(typeSelection==8) {
            (void)reader->rememberPage();
          } else if(typeSelection==14) {
            reader->flushPosition();
            (void)reader->pushReadingSync();
          } else if(typeSelection==15) {
            reader->flushPosition();
            if(reader->pullReadingSync()) {
              typeMenu=false;
              reader->present();
              continue;
            }
          } else if(typeSelection==11) {
            linkMenu=true;
            navigationSelection=0;
          } else if(typeSelection==13) {
            if(reader->beginTextSelection()) {
              selectionChoosing=true;selectionExtending=false;
              typeMenu=false;
              reader->showSelectionPreview();
              continue;
            }
          } else if(typeSelection==12) {
            if(reader->goBackFromLink()) {
              typeMenu=false;
              reader->present();
              continue;
            }
          } else if(typeSelection>=9) {
            showingBookmarks=typeSelection==9;
            navigationMenu=true;
            navigationSelection=showingBookmarks?0:reader->currentChapter();
          } else if(typeSelection>=2)(void)reader->adjustPreference(typeSelection);
          else {
            auto next=reader->typography();
            if(typeSelection==0) {
              next.family=next.family==crosspod::Typography::Family::Sans?
                crosspod::Typography::Family::Serif:crosspod::Typography::Family::Sans;
            } else next.size=12; // Only 12pt fonts are verified in the Q2 package.
            (void)reader->configureTypography(next);
          }
          if(navigationMenu)
            reader->showNavigationMenu(showingBookmarks,navigationSelection);
          else if(linkMenu)
            reader->showLinksMenu(navigationSelection);
          else reader->showTypographyMenu(typeSelection);
        }
        continue;
      }
      // Hold a word or drag between words to select text. Short taps retain
      // the existing link and left/right page controls. Q2 socket reports
      // touch down/move/up even though ordinary readers used only release.
      if(event.kind==crosspod::PhysicalKind::TouchDown) {
        touchPressed=true;touchStartX=event.x;touchStartY=event.y;
        touchStartTime=std::chrono::steady_clock::now();
        continue;
      }
      if(event.kind==crosspod::PhysicalKind::TouchUp && touchPressed) {
        touchPressed=false;
        const auto elapsed=std::chrono::steady_clock::now()-touchStartTime;
        const int dx=event.x-touchStartX,dy=event.y-touchStartY;
        const bool deliberate=elapsed>=std::chrono::milliseconds(450) ||
             dx*dx+dy*dy>=28*28;
        if(deliberate && touchStartY>=TOP && touchStartY<BOTTOM &&
           reader->selectTouchWord(touchStartX,touchStartY,false)) {
          (void)reader->selectTouchWord(event.x,event.y,true);
          selectionAction=0;selectionActions=true;
          reader->showTextSelectionMenu(0);
          continue;
        }
      }
      if(action==crosspod::LogicalAction::Confirm ||
         (event.kind==crosspod::PhysicalKind::TouchUp && event.y<45)) {
        typeMenu=true;
        typeSelection=0;
        reader->showTypographyMenu(typeSelection);
        continue;
      }
      if(action==crosspod::LogicalAction::Back) {
        (void)reader->cacheShelfCover();
        reader.reset(); // close book/cache BEFORE reopening the card shelf
        crosspod::sortShelfBooks(books,sortMode,recent);
        selection=crosspod::lastOpenedIndex(books,recent);
        shelf.reset(new CardShelf(books,selection,selection,sortMode));
        if(!shelf->open())return 6;
        shelf->present();
        continue;
      }
      bool changed=false;
      if(action==crosspod::LogicalAction::Next) changed=reader->move(1);
      if(action==crosspod::LogicalAction::Previous)changed=reader->move(-1);
      if(event.kind==crosspod::PhysicalKind::TouchUp) {
        const auto href=reader->touchedLink(event.x,event.y);
        if(!href.empty()) {
          if(reader->isFootnoteLink(href)) {
            footnoteHref=href;footnoteOverlay=true;
            reader->showFootnotePopup(href);
            continue;
          }
          changed=reader->followLink(href);
        } else changed=reader->move(event.x>=187?1:-1);
      }
      if(changed)reader->present();
      continue;
    }
    if(shelf->onlineMode()) {
      if(action==crosspod::LogicalAction::Back) {
        shelf->returnFromOnline();
        shelf->present();continue;
      }
      if(action==crosspod::LogicalAction::Next ||
         action==crosspod::LogicalAction::Previous) {
        if(shelf->change(action==crosspod::LogicalAction::Next?1:-1))
          shelf->present();
        continue;
      }
      if(event.kind==crosspod::PhysicalKind::TouchUp && event.y<73) {
        shelf->returnFromOnline();shelf->present();continue;
      }
      bool activate=action==crosspod::LogicalAction::Confirm;
      if(event.kind==crosspod::PhysicalKind::TouchUp) {
        shelf->onlineTouch(event.x,event.y);
        activate=true;
      }
      if(!activate || !shelf->onlineHasSelection())continue;
      const auto selected=shelf->onlineEntry();
      if(selected.kind==crosspod::OpdsEntry::Kind::Catalog) {
        shelf->setNotice("Loading catalog...");
        shelf->present();
        (void)shelf->loadOnlinePage(selected.href,true);
        shelf->present();
        continue;
      }
      shelf->setNotice("Downloading EPUB...");
      shelf->present();
      std::string saved,error;
      if(!crosspod::opdsDownloadEpub(bookDir,selected,saved,error)) {
        shelf->setNotice(error.empty()?"Download failed":error);
        shelf->present();continue;
      }
      // The reader only sees fully atomically renamed EPUB ZIPs. Rescan the
      // bounded library after a download, never while a .part is in flight.
      books=scanCardBooks(bookDir);
      crosspod::sortShelfBooks(books,sortMode,recent);
      auto found=std::find_if(books.begin(),books.end(),
          [&](const ShelfEntry& entry){return entry.path==saved;});
      selection=found==books.end()?0:
          static_cast<size_t>(found-books.begin());
      shelf.reset(new CardShelf(books,selection,selection,sortMode));
      if(!shelf->open())return 6;
      shelf->enterBrowser();
      shelf->setNotice("EPUB downloaded to Books/Online");
      shelf->present();
      nextShelfIndex=0;
      continue;
    }
    if(shelf->searching()) {
      if(action==crosspod::LogicalAction::Back)
        shelf->endSearch();
      else if(action==crosspod::LogicalAction::Previous)
        (void)shelf->change(-1);
      else if(action==crosspod::LogicalAction::Next)
        (void)shelf->change(1);
      else if(action==crosspod::LogicalAction::Confirm)
        (void)shelf->applySearchChoice();
      else if(event.kind==crosspod::PhysicalKind::TouchUp)
        (void)shelf->tap(event.x,event.y);
      shelf->present();
      continue;
    }
    if(action==crosspod::LogicalAction::Back) {
      if(shelf->browsing()) {shelf->goHome();shelf->present();continue;}
      break;
    }
    if(action==crosspod::LogicalAction::Previous && shelf->change(-1))shelf->present();
    if(action==crosspod::LogicalAction::Next && shelf->change(1))shelf->present();
    bool activate=action==crosspod::LogicalAction::Confirm;
    if(event.kind==crosspod::PhysicalKind::TouchUp && shelf->browsing() &&
       event.y>=39 && event.y<74 && event.x>=190) {
      (void)shelf->enterOnline();
      shelf->present();
      continue;
    }
    if(event.kind==crosspod::PhysicalKind::TouchUp)activate=shelf->tap(event.x,event.y);
    if(!activate)continue;
    if(shelf->searching()) {shelf->present();continue;}
    if(shelf->onlineChosen()) {
      shelf->setNotice("Loading catalog...");
      shelf->present();
      (void)shelf->enterOnline();
      shelf->present();
      continue;
    }
    if(shelf->sortChosen()) {
      sortMode=sortMode==crosspod::ShelfSortMode::Title?
          crosspod::ShelfSortMode::Recent:
          (sortMode==crosspod::ShelfSortMode::Recent?
          crosspod::ShelfSortMode::RecentlyAdded:
          crosspod::ShelfSortMode::Title);
      crosspod::sortShelfBooks(books,sortMode,recent);
      selection=crosspod::lastOpenedIndex(books,recent);
      shelf.reset(new CardShelf(books,selection,selection,sortMode));
      if(!shelf->open())return 6;
      shelf->present();
      continue;
    }
    if(shelf->browseChosen()) {
      shelf->enterBrowser();
      shelf->present();
      continue;
    }
    if(!shelf->hasMatches())continue;
    selection=shelf->selected();
    const std::string chosen=shelf->bookPath();
    shelf->setNotice("Opening EPUB...");
    shelf->present();
    auto next=std::make_unique<Reader>(chosen);
    const bool opened=next->open(true);
    if(interrupted)break;
    if(!opened) {
      shelf->setNotice("Cannot read this EPUB");
      shelf->present();
    } else {
      // A successful open updates Continue Reading; rejected/corrupt EPUBs
      // never become the current title. Atomic history costs one SD write
      // per opened book, never one write per page turn.
      shelf.reset();
      reader=std::move(next);
      reader->present();
      (void)crosspod::saveShelfDetails(chosen,reader->shelfDetails());
      if(crosspod::recordRecentBook(chosen))
        recent=crosspod::loadRecentBooks();
    }
  }
  return 0;
}

void usage() {
  std::fputs(
    "Usage:\n"
    "  crosspod-reader --validate <book.epub>       (no framebuffer)\n"
    "  crosspod-reader --walk <book.epub>           (test pagination)\n"
    "  crosspod-reader --bench <book.epub>          (latency & memory)\n"
    "  crosspod-reader --list-books <directory>     (card scanner test)\n"
    "  crosspod-reader --test-page-links <book.epub> (real link regression)\n"
    "  crosspod-reader --index-book <book.epub>     (OPF/cover index test)\n"
    "  crosspod-reader --card-preview <dir> <out.pgm> (menu raster test)\n"
    "  crosspod-reader --card-search-preview <dir> <out.pgm> (search UI test)\n"
    "  crosspod-reader (no args)                    (Q2 card app)\n"
    "  crosspod-reader --render <book.epub> <page.pgm> (no framebuffer)\n"
    "  crosspod-reader <book.epub>                  (Q2 Pod Books handoff)\n"
    "On Q2, only run interactively after Q2 Pod yields /dev/fb0.\n",stderr);
}
} // namespace

int main(int argc,char** argv) {
  // Card apps start with NO book argument; the on-device picker owns launch.
  if(argc==1) return runCardApp("/mnt/mmc/Books");
  if(argc==3 && std::strcmp(argv[1],"--opds-probe")==0) {
    const auto catalog=crosspod::fetchOpdsFeed(argv[2]);
    if(!catalog.error.empty()) {
      std::fprintf(stderr,"CrossPod OPDS request: %s\n",catalog.error.c_str());
      return 8;
    }
    std::printf("CrossPod OPDS: %s, entries=%zu\n",
                catalog.title.c_str(),catalog.entries.size());
    for(const auto& entry:catalog.entries)
      std::printf("%s: %s\n",
        entry.kind==crosspod::OpdsEntry::Kind::Epub?"EPUB":"CATALOG",
        entry.title.c_str());
    return 0;
  }
  if(argc==4 && std::strcmp(argv[1],"--opds-download")==0) {
    auto catalog=crosspod::fetchOpdsFeed(argv[2]);
    if(!catalog.error.empty())return 8;
    for(const auto& entry:catalog.entries) {
      if(entry.kind!=crosspod::OpdsEntry::Kind::Epub)continue;
      std::string saved,error;
      if(!crosspod::opdsDownloadEpub(argv[3],entry,saved,error)) {
        std::fprintf(stderr,"CrossPod download: %s\n",error.c_str());
        return 9;
      }
      std::printf("CrossPod OPDS downloaded: %s\n",saved.c_str());
      return 0;
    }
    return 10;
  }
  if(argc==3 && std::strcmp(argv[1],"--koreader-hash")==0) {
    const auto md5=crosspod::koReaderPartialMd5(argv[2]);
    if(md5.empty())return 9;
    std::printf("CrossPod KOReader partial MD5: %s\n",md5.c_str());
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-koreader-sync")==0) {
    Reader reader(argv[2]);
    if(!reader.open(true))return 3;
    if(reader.chapterCount()>1 && !reader.goToChapter(1))return 4;
    if(!reader.pushReadingSync())return 5;
    if(!reader.pullReadingSync())return 6;
    std::puts("CrossPod KOReader XPath progress roundtrip: PASS");
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--index-book")==0) {
    // Host/QEMU regression: metadata and an EPUB3 cover-image must become
    // readable cached shelf files WITHOUT constructing a Reader or Section.
    const auto result=crosspod::indexShelfBook(argv[2]);
    crosspod::ShelfDetails details;
    crosspod::ShelfCover cover;
    const bool haveMetadata=crosspod::loadShelfDetails(argv[2],details);
    const bool haveCover=crosspod::loadShelfCover(argv[2],cover);
    std::printf("CrossPod shelf index: metadata=%d cover=%d updated=%d title=%s author=%s\n",
       haveMetadata,haveCover,result.updated,
       haveMetadata?details.title.c_str():"",
       haveMetadata?details.author.c_str():"");
    return haveMetadata && haveCover?0:5;
  }
  if(argc==3 && std::strcmp(argv[1],"--list-books")==0) {
    const auto books=scanCardBooks(argv[2]);
    for(const auto& b:books)std::printf("%s\n",b.path.c_str());
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-footnote-popup")==0) {
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    for(const auto& link:reader.pageLinks()) {
      if(!reader.isFootnoteLink(link))continue;
      const auto preview=reader.footnotePreview(link);
      if(preview.find("This footnote is a short note")==std::string::npos)
        return 8;
      reader.showFootnotePopup(link);
      std::puts("CrossPod XHTML footnote popover: PASS");
      return 0;
    }
    std::puts("CrossPod XHTML footnote noteref missing");
    return 7;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-selection")==0) {
    Reader reader(argv[2]);
    if(!reader.open() || !reader.beginTextSelection())return 7;
    const auto selected=reader.selectedText();
    if(selected.empty())return 8;
    if(!reader.stepSelection(1))return 9;
    if(!reader.saveSelectionHighlight())return 10;
    const auto recorded=crosspod::loadHighlights(argv[2]);
    if(recorded.empty() || recorded.front().begin>=recorded.front().end)
      return 11;
    reader.present(); // highlights must draw over actual Section page pixels
    if(!reader.eraseSelectionHighlight())return 12;
    if(!crosspod::loadHighlights(argv[2]).empty())return 13;
    std::printf("CrossPod unicode selection and persistent highlight: PASS (%s)\n",
                selected.c_str());
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-page-links")==0) {
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    const auto links=reader.pageLinks();
    std::printf("CrossPod Page links: %zu\n",links.size());
    if(links.size()<2)return 7;
    bool followed=false;
    for(const auto& href:links) {
      if(href.find("second.xhtml")!=std::string::npos) {
        if(!reader.followLink(href))return 8;
        followed=true;break;
      }
    }
    if(!followed || !reader.hasLinkHistory())return 9;
    if(!reader.goBackFromLink() || reader.hasLinkHistory())return 10;
    std::puts("CrossPod page link jump and return: PASS");
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--inspect-toc")==0) {
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    reader.dumpToc();
    return 0;
  }
  if(argc==4 && std::strcmp(argv[1],"--render-images")==0) {
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    if(reader.imageCount()==0)return 4;
    std::printf("CrossPod rendered images: %zu\n",reader.imageCount());
    return reader.savePgm(argv[3])?0:5;
  }
  if(argc==4 && std::strcmp(argv[1],"--inspect-styles")==0) {
    // ZipFile retains a const std::string&: the path must outlive the archive.
    const std::string epubPath(argv[2]);
    ZipFile zip(epubPath);
    if(!zip.loadAllFileStatSlims())return 3;
    std::vector<Paragraph> paragraphs;
    if(!parseChapter(zip,epubPath,argv[3],paragraphs))return 4;
    for(size_t i=0;i<paragraphs.size();++i) {
      const auto& p=paragraphs[i];
      bool bold=false,italic=false;
      std::string content;
      for(const auto& run:p.runs) {
        bold=bold||((run.style&EpdFontFamily::BOLD)!=0);
        italic=italic||((run.style&EpdFontFamily::ITALIC)!=0);
        content+=run.text;
      }
      std::printf("paragraph=%zu align=%d rtl=%d bold=%d italic=%d mt=%d mb=%d ml=%d mr=%d indent=%d break_before=%d break_after=%d text=%s\n",
        i,static_cast<int>(p.align),p.rtl,bold,italic,
        p.block.marginTop,p.block.marginBottom,p.block.marginLeft,
        p.block.marginRight,p.block.textIndent,
        p.block.pageBreakBefore,p.block.pageBreakAfter,content.c_str());
    }
    return 0;
  }
  if(argc==4 && std::strcmp(argv[1],"--card-preview")==0) {
    const auto books=scanCardBooks(argv[2]);
    CardShelf shelf(books);
    return shelf.open() && shelf.savePgm(argv[3])?0:4;
  }
  if(argc==4 && std::strcmp(argv[1],"--card-search-preview")==0) {
    const auto books=scanCardBooks(argv[2]);
    CardShelf shelf(books);
    if(!shelf.open())return 4;
    shelf.enterBrowser();
    shelf.enterSearch();
    return shelf.savePgm(argv[3])?0:4;
  }
  bool dry=false,render=false,walk=false,renderWalk=false,benchmark=false;
  std::string path,output;
  if(argc==3 && std::strcmp(argv[1],"--validate")==0) {
    dry=true;path=argv[2];
  } else if(argc==3 && std::strcmp(argv[1],"--walk")==0) {
    dry=true;walk=true;path=argv[2];
  }
  else if(argc==3 && std::strcmp(argv[1],"--render-walk")==0) {
    dry=true;renderWalk=true;path=argv[2];
  } else if(argc==3 && std::strcmp(argv[1],"--bench")==0) {
    dry=true;benchmark=true;path=argv[2];
  } else if(argc==4 && std::strcmp(argv[1],"--render")==0) {
    dry=true;render=true;path=argv[2];output=argv[3];
  } else if (argc==2 && argv[1][0] != '-') {
    path=argv[1];
  } else { usage();return 2; }
  if (path.size()<5 || path.substr(path.size()-5)!=".epub") {
    std::fputs("CrossPod: only EPUB is supported in this device preview\n",stderr);
    return 2;
  }
  if(!dry && !installExitHandlers())return 5;
  // Destroy the reader and flush progress before releasing the input socket.
  crosspod::Q2SocketInput input;
  const auto openStart=std::chrono::steady_clock::now();
  Reader reader(std::move(path));
  const bool opened=reader.open(!dry);
  if(interrupted)return 0;
  if(!opened) {
    std::fputs("CrossPod: EPUB not supported by current reader slice\n",stderr);
    return 3;
  }
  if(dry) {
    if(benchmark) {
      const auto openEnd=std::chrono::steady_clock::now();
      reader.render(); // includes the first page's SD read and glyph paint
      const auto firstRenderEnd=std::chrono::steady_clock::now();
      // Reuse the identical raster; never repeatedly inflate EPUB pages.
      constexpr int REPEAT_FRAMES=32;
      for(int i=0;i<REPEAT_FRAMES;++i)reader.render();
      const auto repeatedEnd=std::chrono::steady_clock::now();
      size_t advanced=0;
      constexpr size_t SAMPLE_PAGES=64;
      while(advanced<SAMPLE_PAGES) {
        if(reader.move(1)) {++advanced;continue;}
        if(!reader.isBuilding())break;
        (void)reader.buildNextPages(4);
      }
      const auto elapsed=std::chrono::steady_clock::now();
      struct rusage usage{};
      (void)getrusage(RUSAGE_SELF,&usage);
      const auto openMs=std::chrono::duration_cast<std::chrono::milliseconds>(
        openEnd-openStart).count();
      const auto firstRenderUs=std::chrono::duration_cast<std::chrono::microseconds>(
        firstRenderEnd-openEnd).count();
      const auto cachedRenderUs=std::chrono::duration_cast<std::chrono::microseconds>(
        repeatedEnd-firstRenderEnd).count()/REPEAT_FRAMES;
      const auto turnMs=std::chrono::duration_cast<std::chrono::milliseconds>(
        elapsed-repeatedEnd).count();
      std::printf(
        "CrossPod benchmark: open_ms=%lld first_render_us=%lld "
        "cached_render_us=%lld sample_pages=%zu walk_ms=%lld "
        "maxrss_kb=%ld read_blocks=%ld written_blocks=%ld\n",
        static_cast<long long>(openMs),
        static_cast<long long>(firstRenderUs),
        static_cast<long long>(cachedRenderUs),
        advanced,static_cast<long long>(turnMs),
        usage.ru_maxrss,usage.ru_inblock,usage.ru_oublock);
      return 0;
    }
    if(renderWalk) {
      // Exercise the actual font/glyph/page rasterizer, not just page indexes.
      // This catches the blank-reader regression after prolonged wheel turns.
      size_t forward=0,backward=0;
      reader.render();
      if(!reader.lastRenderSucceeded() || !reader.pageHasVisibleInk())return 9;
      while(forward<2048) {
        if(reader.move(1)) {
          reader.render();
          if(!reader.lastRenderSucceeded() || !reader.pageHasVisibleInk()) {
            std::fprintf(stderr,"CrossPod: blank rendered page after %zu forward turns\n",
                         forward+1);
            return 9;
          }
          ++forward;
          continue;
        }
        if(reader.isBuilding()) {
          (void)reader.buildNextPages(4);
          continue;
        }
        break;
      }
      if(forward>=2048)return 7;
      while(backward<2048 && reader.move(-1)) {
        reader.render();
        if(!reader.pageHasVisibleInk())return 9;
        ++backward;
      }
      if(backward!=forward)return 8;
      std::printf("CrossPod rendered %zu pages forward and %zu backward without blank text\n",
                  forward,backward);
      return forward>=10?0:8;
    }
    if(walk) {
      // Exercise real page/spine transitions and the backward boundary.
      // Limit iterations defensively for corrupt EPUBs.
      size_t forward=0,backward=0;
      while(forward<65536) {
        if(reader.move(1)) {++forward;continue;}
        // A page may not have been produced by the latest incremental
        // parse chunk; don't mistake its temporary watermark for EOF.
        if(reader.isBuilding()) {
          (void)reader.buildNextPages(4);
          continue;
        }
        break;
      }
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
  if(!input.open()) {
    std::fputs("CrossPod: input socket unavailable, refusing interactive mode\n",stderr);
    return 5;
  }
  reader.present();
  bool typeMenu=false;
  int typeSelection=0;
  crosspod::Q2Controls controls(loadWheelSensitivity());
  while(!interrupted) {
    reader.flushPosition(false);
    crosspod::PhysicalEvent event{};
    if (!input.receive(event)) {
      if(waitForReaderInput(input,reader.nextPositionFlush())<0) {
        std::fputs("CrossPod: input socket poll failed\n",stderr);
        return 5;
      }
      continue;
    }
    if(interrupted)break;
    auto action=controls.translate(event).action;
    bool changed=false;
    if(typeMenu) {
      if(action==crosspod::LogicalAction::Back) {
        typeMenu=false;
        reader.present();
      } else if(action==crosspod::LogicalAction::Previous ||
                action==crosspod::LogicalAction::Next) {
        typeSelection=(typeSelection+
            (action==crosspod::LogicalAction::Next?1:6))%7;
        reader.showTypographyMenu(typeSelection);
      } else if(action==crosspod::LogicalAction::Confirm ||
                event.kind==crosspod::PhysicalKind::TouchUp) {
        if(event.kind==crosspod::PhysicalKind::TouchUp)
          typeSelection=std::clamp((event.y-73)/21,0,6);
        if(typeSelection>=2)(void)reader.adjustPreference(typeSelection);
        else {
          auto next=reader.typography();
          if(typeSelection==0)
            next.family=next.family==crosspod::Typography::Family::Sans?
              crosspod::Typography::Family::Serif:crosspod::Typography::Family::Sans;
          else next.size=12; // Only 12pt fonts are verified in the Q2 package.
          (void)reader.configureTypography(next);
        }
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
  return 0;
}
