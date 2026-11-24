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
#include <Epub/XmlRepairPipeline.h>
#include <Epub/blocks/ImageBlock.h>
#include <Q2Controls.hpp>
#include <SdCardFont.h>
#include "Typography.hpp"
#include "ScriptFonts.hpp"
#include "ReadingPosition.hpp"
#include "ReaderPreferences.hpp"
#include "TxtPackage.hpp"
#include "T9Keypad.hpp"
#include "LegacyStateMigration.hpp"
#include "About.hpp"
#include "Clippings.hpp"
#include "UiLanguage.hpp"
#include "Q2Theme.hpp"
#include "StatusBar.hpp"
#include "MenuLayout.hpp"
#include "UiText.hpp"
#include "SmoothGlyphs.hpp"
#include "ReadingLayout.hpp"
#include "Utf8Label.hpp"
#include "ShelfHistory.hpp"
#include "ShelfSort.hpp"
#include "ShelfFolders.hpp"
#include "ShelfDetails.hpp"
#include "ShelfCover.hpp"
#include "FlowRenderer.hpp"
#include "ShelfProgress.hpp"
#include "ShelfIndexer.hpp"
#include "ReaderLinks.hpp"
#include "Highlights.hpp"
#include "SelectionGeometry.hpp"
#include "OfflineDictionary.hpp"
#include "FootnotePreview.hpp"
#include "OpdsCatalog.hpp"
#include "OpdsCover.hpp"
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
// The page's text area. The Q2's glass rounds its corners (80 px radius, as
// Q2 Pod calibrates it): between y 40 and 282 a 14 px margin clears them; the
// bands above and below hold the status and the progress (drawProgress).
constexpr int SCREEN_H = 320, LEFT = 14, TOP = 40;
constexpr int TEXT_WIDTH = 345, BOTTOM = 282;
volatile std::sig_atomic_t interrupted = 0;
std::string clippedLabel(const std::string&,size_t);
void exitOnSignal(int) { interrupted = 1; }

bool installExitHandlers() {
  struct sigaction action{};
  action.sa_handler=exitOnSignal;
  sigemptyset(&action.sa_mask);
  if(sigaction(SIGINT,&action,nullptr)==0 && sigaction(SIGTERM,&action,nullptr)==0)
    return true;
  std::perror("Books: cannot install exit handlers");
  return false;
}

using ReaderClock = std::chrono::steady_clock;
constexpr auto positionSaveInterval = std::chrono::seconds(5);
// Reading settings rows (showTypographyMenu): 16 settings/actions, then Focus
// reading (16), Auto turn (17) and Go to percent (18).
constexpr int kTypographyItems = 22;  // 19: About, 20: Dictionaries, 21: Export notes (menus follow Q2 Pod's language)

// A finite ceiling also covers a shutdown signal arriving just before poll.
int waitForReaderInput(const books::Q2SocketInput& input,ReaderClock::time_point deadline) {
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
  if(!config)return books::defaultWheelSensitivity;
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
      return books::parseWheelSensitivity(trim(line.substr(equals+1)));
  }
  return books::defaultWheelSensitivity;
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
    const char* value=std::getenv("BOOKS_PROFILE_SESSION");
    enabled_=value && std::strcmp(value,"1")==0 && getrusage(RUSAGE_SELF,&initial_)==0;
  }
  ~SessionProfile() {
    struct rusage usage{};
    if(!enabled_ || getrusage(RUSAGE_SELF,&usage)!=0)return;
    const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(
        ReaderClock::now()-start_).count();
    std::fprintf(stderr,
        "Books session: elapsed_ms=%lld user_cpu_us=%lld system_cpu_us=%lld "
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
  XmlRepairPipeline pipeline_;
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
    const bool fed=pipeline_.push(reinterpret_cast<const char*>(data),len,false,
        [this](const char* d,size_t n){
          return XML_Parse(parser_,d,static_cast<int>(n),XML_FALSE)!=XML_STATUS_ERROR;
        });
    if(!fed) {
      failed_=true;
      return 0;
    }
    return len;
  }
  bool finish() {
    if(failed_ || seen_==0)return false;
    // Flushes repair state held back at the end of the chapter (an unfinished
    // entity, open elements) before the final expat call.
    const bool fed=pipeline_.push(nullptr,0,true,[this](const char* d,size_t n){
      return XML_Parse(parser_,d,static_cast<int>(n),XML_FALSE)!=XML_STATUS_ERROR;
    });
    return fed && XML_Parse(parser_,nullptr,0,XML_TRUE)==XML_STATUS_OK;
  }
};

bool parseChapter(ZipFile& archive,const std::string& bookPath,
                  const std::string& name,std::vector<Paragraph>& out) {
  size_t size=0;
  if(!archive.getInflatedFileSize(name.c_str(),&size)||!size||size>MAX_XML_BYTES) {
    std::fprintf(stderr,"Books: unsupported XHTML chapter length: %s (%zu)\n",name.c_str(),size);
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
  if(!ok)std::fprintf(stderr,"Books: XHTML stream/parse error %s: %s\n",
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
    char dir[]="/tmp/books-epub-XXXXXX";
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
      std::fprintf(stderr,"Books: temporary cache at %s requires cleanup\n",dir_.c_str());
  }
};

struct RenderLine {
  std::unique_ptr<TextBlock> block;
  int y = 0;
};
struct RenderImage {
  books::PngPreview png;
  int x=0,y=0,width=0,height=0;
};
struct RenderPage {
  std::vector<RenderLine> lines;
  std::vector<RenderImage> images;
};
struct DictionaryPage {std::string header;std::vector<std::string> lines;};
class Reader {
  std::string dictionaryWord_;
  std::vector<DictionaryPage> dictionaryPages_;
  int dictionaryPage_=0;
  std::string bookPath_;
  // The ZIP the engine opens: the book itself, or for a .txt its cached EPUB
  // package (TxtPackage.hpp). bookPath_ stays the book's identity.
  std::string packagePath_;
  ZipFile archive_;
  std::unique_ptr<TempCache> temporary_;
  std::unique_ptr<BookMetadataCache> legacyMetadata_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont bodyFont_;
  books::SmoothGlyphs smoothGlyphs_;  // book text from the fonts' TrueType files
  std::vector<uint8_t> cpfontAlpha_;  // the last page's coverage as CrossPoint drew it
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  std::unique_ptr<SdCardFont> selectedFont_;
  books::ScriptFontFallback scriptFonts_;
  // Keep the complete upstream EPUB parser/Section lifecycle alive for this
  // book. Books's prior collector remains a fail-safe fallback.
  std::shared_ptr<Epub> upstreamEpub_;
  std::unique_ptr<Section> upstreamSection_;
  int upstreamFontId_=0;
  books::Typography typography_{};
  books::ReaderPreferences preferences_{};
  std::string typographyNotice_;
  std::vector<RenderPage> pages_;
  // A bounded 3-page 1-bpp raster cache (~45 KB) avoids repeated SD-page
  // decompression, layout painting and glyph lookups when moving back/forward.
  struct CachedRaster {
    int spine=-1;
    size_t page=0;
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> alpha,expect;  // smooth text's coverage layer (empty: none)
  };
  std::array<CachedRaster,3> rasterCache_{};
  books::DeviceStatus status_{};  // what the footer last showed
  size_t nextRasterSlot_=0;
  bool lastRenderOk_=true;
  void clearRasterCache() {
    for(auto& item:rasterCache_)item.spine=-1;
  }
  std::vector<books::Highlight> highlights_;
  bool highlightsLoaded_=false;
  std::vector<books::SelectedWord> selectedWords_;
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
  // Smooth book text: each style of a built-in reading family with its
  // TrueType file in .books/fonts/ttf (or BOOKS_TTF_DIR).
  void registerSmoothGlyphs(books::Typography settings,SdCardFont& font,int fontId) {
    const char* slug=books::typographyFamilySlug(settings.family);
    if(!slug || !books::SmoothGlyphs::enabled())return;
    const char* dir=std::getenv("BOOKS_TTF_DIR");
    const std::string root=dir&&*dir?dir:books::typographyRoot()+"/fonts/ttf";
    const EpdFontFamily family=books::sdCardFontFamily(static_cast<uint16_t>(settings.size),font);
    const EpdFontData* regular=family.getData(EpdFontFamily::REGULAR);
    for(auto style:{EpdFontFamily::REGULAR,EpdFontFamily::BOLD,EpdFontFamily::ITALIC,
                    EpdFontFamily::BOLD_ITALIC}) {
      const EpdFontData* data=family.getData(style);
      // A style the family lacks falls back to regular's data: skip it.
      if(!data || (style!=EpdFontFamily::REGULAR && data==regular))continue;
      // The style's own file; a variable font ships one file per slant, so
      // bold uses regular's and bold italic italic's, at weight 700.
      const bool bold=style==EpdFontFamily::BOLD || style==EpdFontFamily::BOLD_ITALIC;
      const auto file=[&](EpdFontFamily::Style s){
        return root+"/"+slug+"-"+books::SmoothGlyphs::styleName(s)+".ttf";
      };
      std::string path=file(style);
      bool own=::access(path.c_str(),R_OK)==0;
      if(!own && bold) {
        path=file(style==EpdFontFamily::BOLD?EpdFontFamily::REGULAR:EpdFontFamily::ITALIC);
        if(::access(path.c_str(),R_OK)!=0)continue;
      } else if(!own)continue;
      const double em=smoothGlyphs_.registerStyle(renderer_,fontId,style,data,path,bold?700:400,own);
      if(em>0)std::fprintf(stderr,"Books smooth glyphs: %s %s at %.2f px em, tops %+.2f px\n",slug,
                           books::SmoothGlyphs::styleName(style),em,smoothGlyphs_.lastTopDiff());
      else std::fprintf(stderr,"Books smooth glyphs: %s %s kept 2-bit (no matching TrueType)\n",slug,
                        books::SmoothGlyphs::styleName(style));
    }
  }
  // After the grey passes: CrossPoint's glyphs that have TrueType are painted
  // out, then drawn again from TrueType at their own pen positions.
  template<typename PagePtr>
  void drawSmoothGlyphs(const PagePtr& page) {
    if(smoothGlyphs_.empty())return;
    cpfontAlpha_.resize(HalDisplay::PIXELS);  // for smoothGlyphAgreement
    if(!display_.currentAlpha(cpfontAlpha_.data()))cpfontAlpha_.clear();
    renderer_.setGlyphHook(&smoothGlyphs_,GfxRenderer::GlyphHookMode::Erase);
    page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
    renderer_.setGlyphHook(&smoothGlyphs_,GfxRenderer::GlyphHookMode::Draw);
    page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
    renderer_.setGlyphHook(nullptr,GfxRenderer::GlyphHookMode::Off);
  }
  int fontIdForTypography(books::Typography value) const {
    return 1000+static_cast<int>(value.family)*7+(value.size-8);
  }
  ReaderRenderSpec upstreamSpec() const {
    return books::makeReaderRenderSpec(preferences_,upstreamFontId_,
        static_cast<uint16_t>(375),static_cast<uint16_t>(BOTTOM-TOP));
  }
  static std::string stateCacheDir() {
    const char* state=std::getenv("BOOKS_STATE_DIR");
    struct stat cardDir{};
    const bool cardMounted=::stat("/mnt/mmc/.books",&cardDir)==0 &&
                          S_ISDIR(cardDir.st_mode);
    const std::string base=state&&*state?state:
                           (cardMounted?"/mnt/mmc/.books":"/tmp");
    return base+"/cache";
  }
  static std::string resolvePackage(const std::string& book) {
    if(!books::isTxtFilename(book))return book;
    ::mkdir(stateCacheDir().c_str(),0755);
    std::string error;
    std::string package=books::packageTxtBook(book,stateCacheDir(),error);
    if(package.empty())std::fprintf(stderr,"Books: TXT: %s\n",error.c_str());
    return package;
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
    // RD-05: a user font slot's file can change between sessions; pages laid
    // out with another file under the same slot must not be reused.
    for(int f=static_cast<int>(books::Typography::Family::User1);
        f<static_cast<int>(books::Typography::Family::Count);++f)
      for(int size=8;size<=14;++size)
        for(unsigned char byte:books::typographyFontIdentity({static_cast<books::Typography::Family>(f),size})) {
          hash^=byte;hash*=1099511628211ull;
        }
    const char* state=std::getenv("BOOKS_STATE_DIR");
    struct stat cardDir{};
    const bool cardMounted=::stat("/mnt/mmc/.books",&cardDir)==0 &&
                          S_ISDIR(cardDir.st_mode);
    const std::string base=state&&*state?state:
                           (cardMounted?"/mnt/mmc/.books":"/tmp");
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
    if(!Storage.openFileForWrite("BOOKS",dest,out))return false;
    const bool ok=self->upstreamEpub_->readItemContentsToStream(source,out,4096);
    out.close();
    if(!ok)Storage.remove(dest);
    return ok;
  }
  bool useTypography(books::Typography settings) {
    if(!books::validTypography(settings))return false;
    auto next=std::make_unique<SdCardFont>();
    if(!next->load(books::typographyFontPath(settings).c_str()) ||
       !next->getEpdFont()) {
      if(settings.family!=books::Typography::Family::Sans || settings.size!=12)
        return false;
      next.reset();
    }
    // Drop SD glyph registrations BEFORE their owning font handles. Rebuild
    // this size's script-coverage index without loading font bitmaps into RAM.
    scriptFonts_.clear(renderer_);
    smoothGlyphs_.clear();  // its keys are the old font's data
    renderer_.removeFont(1);
    if(upstreamFontId_)renderer_.removeFont(upstreamFontId_);
    const int nextFontId=fontIdForTypography(settings);
    if(next && next->getEpdFont()) {
      renderer_.insertFont(1,books::sdCardFontFamily(static_cast<uint16_t>(settings.size),*next));
      renderer_.registerSdCardFont(1,next.get());
      renderer_.insertFont(nextFontId,books::sdCardFontFamily(static_cast<uint16_t>(settings.size),*next));
      renderer_.registerSdCardFont(nextFontId,next.get());
    } else {
      renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
      renderer_.insertFont(nextFontId,EpdFontFamily(12,&bodyFont_));
    }
    if(next && next->getEpdFont())registerSmoothGlyphs(settings,*next,nextFontId);
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
        books::PngPreview png;
        if(!resource)continue;
        const size_t dot=p.imagePath.find_last_of('.');
        std::string extension=dot==std::string::npos?"" :p.imagePath.substr(dot);
        std::transform(extension.begin(),extension.end(),extension.begin(),
                       [](unsigned char c){return static_cast<char>(std::tolower(c));});
        const bool loaded=extension==".png"?
            books::decodePngPreview(resource.get(),bytes,png):
            books::decodeJpegPreview(resource.get(),bytes,png);
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
        std::fputs("Books: chapter exceeds the bounded page cache\n",stderr);
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
      std::fputs("Books: cannot create temporary cache\n",stderr);return false;
    }
    if (!archive_.loadAllFileStatSlims()) {
      std::fputs("Books: cannot index EPUB ZIP\n",stderr);return false;
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
           legacyMetadata_->endWrite() && legacyMetadata_->buildBookBin(packagePath_,info) &&
           legacyMetadata_->load() && legacyMetadata_->getSpineCount()>0;
  }
  BookMetadataCache& metadata() const {
    return upstreamEpub_?upstreamEpub_->getBookMetadataCache():*legacyMetadata_;
  }
 public:
  explicit Reader(std::string path):
      bookPath_(std::move(path)),packagePath_(resolvePackage(bookPath_)),archive_(packagePath_),
      renderer_(display_),
      bodyFont_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()),
      smoothGlyphs_(display_) {}
  ~Reader() {
    // A layout cut short by the exit is not a page to keep: drop that build
    // instead of suspending it, which would cache its pages.
    if(upstreamSection_ && bookslayout::layoutAborted())upstreamSection_->abandonBuild();
    flushPosition();
    ImageBlock::setExtractor(nullptr,nullptr);
  }
  bool open(bool resumeOnOpen=false) {
    (void)books::loadReaderPreferences(preferences_);
    // These are unmodified upstream CrossPoint layout/render sources.
    display_.begin();
    renderer_.setOrientation(GfxRenderer::LandscapeCounterClockwise);
    renderer_.begin();
    books::applyThemePalette(display_);  // Q2 Pod's page colours, when it gives them
    books::UiText::instance().setThemeFace(books::uiTextFace());
    renderer_.insertFont(1,EpdFontFamily(12,&bodyFont_));
    // UI font is always fixed; book typography updates must not touch it.
    renderer_.insertFont(2,EpdFontFamily(12,&bodyFont_));
    if (!decompressor_.init()) return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    books::Typography savedType;
    if(!books::loadTypography(savedType)) {
      (void)useTypography(typography_);
    } else if(!(savedType.size=books::nearestTypographySize(savedType)) || !useTypography(savedType)) {
      typographyNotice_="Selected font file missing";
      (void)useTypography(typography_);
    }
    std::printf("Books typeface: %s %dpt%s\n",
        books::typographyFamilyName(typography_.family).c_str(),typography_.size,
        typographyNotice_.empty()?"":" (saved font unavailable; fallback)");
    std::printf("Books font source: %s\n",
        selectedFont_?"original CrossPoint extended cpfont":"built-in emergency font");
    const char* requestedEngine=std::getenv("BOOKS_ENGINE");
    if(!requestedEngine || std::strcmp(requestedEngine,"legacy")!=0) {
      auto candidate=std::make_shared<Epub>(packagePath_,upstreamCachePath());
      if(candidate->load(true,false)) {
        upstreamEpub_=std::move(candidate);
        ImageBlock::setExtractor(this,&Reader::extractUpstreamImage);
      } else std::fprintf(stderr,
          "Books: original Section unavailable; using legacy collector\n");
    }
    if(!upstreamEpub_ && !loadLegacyMetadata())return false;
    const auto& info=metadata().coreMetadata;
    std::printf("Books EPUB: %s — %s | %d spine entries\n",
        info.title.c_str(),info.author.c_str(),metadata().getSpineCount());
    // Load the saved chapter first; the old startup rendered spine 0 and
    // immediately discarded it when resume opened a later chapter.
    books::ReadingPosition saved{};
    const bool hasSaved=resumeOnOpen &&
        books::loadReadingPosition(bookPath_,saved) &&
        saved.spine>=0 && saved.spine<metadata().getSpineCount();
    const int firstSpine=hasSaved?saved.spine:0;
    bool loaded=loadSpine(firstSpine,1);
    if(!loaded && firstSpine!=0)loaded=loadSpine(0,1);
    if(!loaded)return false;
    if(resumeOnOpen) {
      resumeEnabled_=true;
      if(hasSaved && currentSpine_==saved.spine && pageCount()>0)
        placeSavedPosition(saved);
      positionDirty_=false;
      lastPositionAttempt_=std::chrono::steady_clock::now();
    }
    return true;
  }
  void enableResume() {
    resumeEnabled_=true;
    books::ReadingPosition saved;
    if(!books::loadReadingPosition(bookPath_,saved))return;
    if(!loadSpine(saved.spine,1) || currentSpine_!=saved.spine) {
      (void)loadSpine(0,1);
      return;
    }
    placeSavedPosition(saved);
    positionDirty_=false;
    lastPositionAttempt_=std::chrono::steady_clock::now();
  }
  // The place to save: the page, and what names its content for another
  // engine or a later layout (see books::ReadingPosition).
  books::ReadingPosition currentPosition() const {
    books::ReadingPosition position{currentSpine_,currentPage_};
    if(upstreamSection_) {
      position.engine='o';
      if(auto offset=upstreamSection_->getVisibleTextOffsetForPage(
           static_cast<uint16_t>(currentPage_)))position.textOffset=*offset;
      const size_t total=std::max<size_t>(pageCount(),upstreamSection_->estimatedTotalPages());
      if(total)position.fractionPpm=static_cast<uint32_t>(
          static_cast<uint64_t>(currentPage_)*1000000u/total);
    } else if(!pages_.empty()) {
      position.engine='l';
      position.fractionPpm=static_cast<uint32_t>(
          static_cast<uint64_t>(currentPage_)*1000000u/pages_.size());
    }
    return position;
  }
  // currentSpine_ is the saved chapter: put currentPage_ on the saved content.
  // The original engine finds its own pages by exact text offset; across an
  // engine fallback (a full card, a chapter it cannot lay out) the page index
  // names other content, so the chapter fraction places it instead.
  void placeSavedPosition(const books::ReadingPosition& saved) {
    const int spine=currentSpine_;
    if(upstreamSection_ && saved.engine=='o' && saved.textOffset>0) {
      while(!interrupted && upstreamSection_ && currentSpine_==spine &&
            upstreamSection_->isBuilding() &&
            !upstreamSection_->buildReachedVisibleTextOffset(saved.textOffset))
        (void)buildNextPages(2);
      if(upstreamSection_ && currentSpine_==spine) {
        if(auto page=upstreamSection_->getPageForVisibleTextOffset(saved.textOffset)) {
          currentPage_=std::min<size_t>(*page,pageCount()-1);
          return;
        }
      }
    }
    if(upstreamSection_ && saved.engine=='l') {
      const auto target=[&] {
        const size_t total=std::max<size_t>(pageCount(),upstreamSection_->estimatedTotalPages());
        return static_cast<size_t>(static_cast<uint64_t>(saved.fractionPpm)*total/1000000u);
      };
      while(!interrupted && upstreamSection_ && currentSpine_==spine &&
            upstreamSection_->isBuilding() && pageCount()<=target())
        (void)buildNextPages(2);
      if(upstreamSection_ && currentSpine_==spine && pageCount()>0) {
        currentPage_=std::min(target(),pageCount()-1);
        return;
      }
    }
    if(!upstreamSection_ && saved.engine=='o' && !pages_.empty()) {
      currentPage_=std::min<size_t>(
          static_cast<uint64_t>(saved.fractionPpm)*pages_.size()/1000000u,pages_.size()-1);
      return;
    }
    // Same engine without an anchor, or a version 1 file: the page itself.
    while(!interrupted && currentSpine_==spine && pageCount()<=saved.page && isBuilding())
      (void)buildNextPages(1);
    if(currentSpine_==spine && pageCount()>0)
      currentPage_=std::min(saved.page,pageCount()-1);
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
    if(books::saveReadingPosition(bookPath_,currentPosition())) {
      positionDirty_=false;
      (void)books::saveShelfProgress(bookPath_,bookProgressPpm());
    }
  }
  ReaderClock::time_point nextPositionFlush() const {
    return positionDirty_?lastPositionAttempt_+positionSaveInterval:
                          ReaderClock::time_point::max();
  }
  void flushPosition(bool force=true) const {
    if(positionDirty_)persistPosition(force);
  }
  bool configureTypography(books::Typography requested) {
    if(!books::validTypography(requested))return false;
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
    if(!books::saveTypography(typography_))
      typographyNotice_="Settings not saved to SD";
    else typographyNotice_.clear();
    persistPosition();
    return true;
  }
  books::ShelfDetails shelfDetails() const {
    return {metadata().coreMetadata.title,metadata().coreMetadata.author};
  }
  // Generate a tiny cover once, only after the user has already opened
  // the EPUB. Never inflate cover JPEG/PNG for every bookshelf row or turn.
  bool cacheShelfCover() {
    books::ShelfCover existing;
    if(books::loadShelfCover(bookPath_,existing))return true;
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
    books::PngPreview decoded;
    bool okay=books::decodePngPreview(bytes.get(),size,decoded);
    if(!okay && bytes.get()[0]==0xff && bytes.get()[1]==0xd8)
      okay=books::decodeJpegPreview(bytes.get(),size,decoded);
    if(!okay)return false;
    books::ShelfCover thumbnail;
    return books::makeShelfCover(decoded,thumbnail) &&
           books::saveShelfCover(bookPath_,thumbnail);
  }

  books::Typography typography() const {return typography_;}
  books::ReaderPreferences preferences() const {return preferences_;}
  size_t chapterCount() const {
    if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0)
      return static_cast<size_t>(upstreamEpub_->getTocItemsCount());
    return static_cast<size_t>(metadata().getSpineCount());
  }
  size_t currentPageIndex() const {return currentPage_;}
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
    return books::koReaderPartialMd5(bookPath_);
  }
  // M4: the conflict the last push or pull found (Server ahead on a push,
  // behind on a pull); the card app asks which position to keep.
  struct SyncConflictState {
    books::SyncConflict kind=books::SyncConflict::None;
    books::RemoteProgress remote;
    float local=0;
  };
 private:
  SyncConflictState syncConflict_;
 public:
  const SyncConflictState& syncConflict() const {return syncConflict_;}
  void clearSyncConflict() {syncConflict_={};}
  // This page as a KOReader position (XPath and percentage).
  bool localSyncPosition(books::RemoteProgress& local) {
    if(!upstreamEpub_ || !upstreamSection_)return false;
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
    if(mapped.xpath.empty())return false;
    local={mapped.xpath,std::clamp(mapped.percentage,0.f,1.f),true,{}};
    return true;
  }
  // Upload this page. With |force| false, a server further on (another
  // device read ahead) is not overwritten: the conflict is kept instead.
  bool pushReadingSync(bool force=false) {
    syncConflict_={};
    const auto config=books::loadConnectedConfig();
    if(!config.syncConfigured() || !upstreamEpub_ || !upstreamSection_) {
      typographyNotice_="Enable sync in connected.conf";
      return false;
    }
    const auto hash=syncDocumentHash();
    if(hash.empty()) {
      typographyNotice_="Cannot hash EPUB";
      return false;
    }
    books::RemoteProgress local{};
    if(!localSyncPosition(local)) {
      typographyNotice_="Cannot map text position";
      return false;
    }
    std::string error;
    if(!force) {
      books::RemoteProgress remote{};
      if(!books::pullKoreaderProgress(config,hash,remote,error)) {
        typographyNotice_="Sync check: "+error;
        return false;
      }
      if(books::syncConflict(remote,local.progress,local.percentage)==books::SyncConflict::ServerAhead) {
        syncConflict_={books::SyncConflict::ServerAhead,remote,local.percentage};
        typographyNotice_="Remote ahead. Pull first.";
        return false;
      }
    }
    if(!books::pushKoreaderProgress(config,hash,local,error)) {
      typographyNotice_="Sync upload: "+error;
      return false;
    }
    typographyNotice_="Progress uploaded";
    return true;
  }
  // Go to the server's position. With |force| false, a server behind this
  // page (an older position) is not applied: the conflict is kept instead.
  bool pullReadingSync(bool force=false) {
    syncConflict_={};
    const auto config=books::loadConnectedConfig();
    if(!config.syncConfigured() || !upstreamEpub_ || !upstreamSection_) {
      typographyNotice_="Enable sync in connected.conf";
      return false;
    }
    const auto hash=syncDocumentHash();
    if(hash.empty()) {
      typographyNotice_="Cannot hash EPUB";
      return false;
    }
    books::RemoteProgress remote{};
    std::string error;
    if(!books::pullKoreaderProgress(config,hash,remote,error)) {
      typographyNotice_="Sync download: "+error;
      return false;
    }
    if(!remote.exists) {
      typographyNotice_="No remote progress";
      return false;
    }
    books::RemoteProgress local{};
    if(!force && localSyncPosition(local) &&
       books::syncConflict(remote,local.progress,local.percentage)==books::SyncConflict::ServerBehind) {
      syncConflict_={books::SyncConflict::ServerBehind,remote,local.percentage};
      typographyNotice_="Remote is behind";
      return false;
    }
    return applyRemoteProgress(remote);
  }
  bool applyRemoteProgress(const books::RemoteProgress& remote) {
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
  // The choice: the two positions, then what to do. |choice| 0 takes the
  // server's position, 1 keeps (and uploads) this Q2's, 2 cancels.
  void showSyncConflict(int choice) {
    const auto& c=syncConflict_;
    renderer_.clearScreen(0xff);
    applyMenuPalette();
    renderer_.fillRect(10,40,355,236,true);
    renderer_.fillRect(12,42,351,232,false);
    books::paintFrame(display_,10,40,355,236,2);
    drawMenuText(22,52,"Reading positions differ");
    renderer_.fillRect(18,71,337,1,true);
    const int server=static_cast<int>(c.remote.percentage*100.f+0.5f);
    const int here=static_cast<int>(c.local*100.f+0.5f);
    std::string line=std::string(books::tr("This Q2: "))+std::to_string(here)+"%";
    drawMenuText(22,78,line.c_str());
    line=std::string(books::tr("Server: "))+std::to_string(server)+"%";
    if(!c.remote.device.empty())line+=" ("+clippedLabel(c.remote.device,18)+")";
    drawMenuText(22,99,line.c_str());
    drawMenuText(22,124,c.kind==books::SyncConflict::ServerAhead?
        "Another device read further.":"The server's position is older.",1);
    const std::string options[3]={
      std::string(books::tr("Go to the server's "))+std::to_string(server)+"%",
      std::string(books::tr(c.kind==books::SyncConflict::ServerAhead?"Upload this Q2's ":"Keep and upload "))+
          std::to_string(here)+"%",
      books::tr("Cancel")};
    for(int i=0;i<3;++i) {
      const int y=146+i*36;
      drawMenuText(26,y+8,options[i].c_str());
      if(i==choice)invertMenuRect(18,y,339,32);
    }
    display_.displayBuffer();
  }
  void dumpToc() {
    if(!upstreamEpub_)return;
    const int count=upstreamEpub_->getTocItemsCount();
    std::printf("Books TOC entries: %d\n",count);
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
  std::vector<books::Bookmark> jumpHistory_;
  bool followLink(const std::string& href) {
    if(currentSpine_<0 || !upstreamSection_ || !upstreamEpub_)return false;
    const auto current=metadata().getSpineEntry(currentSpine_).href;
    const auto destination=books::resolveInternalLink(current,href);
    if(!destination.valid)return false;
    int target=-1;
    for(int i=0;i<metadata().getSpineCount();++i) {
      if(metadata().getSpineEntry(i).href==destination.path) {
        target=i;break;
      }
    }
    if(target<0)return false;
    if(jumpHistory_.size()>=16)jumpHistory_.erase(jumpHistory_.begin());
    const auto original=books::Bookmark{
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
    return books::loadFootnotePreview(archive_,
      metadata().getSpineEntry(currentSpine_).href,href);
  }
  void showFootnotePopup(const std::string& href) {
    std::string note=footnotePreview(href);
    render();
    renderer_.fillRect(10,58,355,250,true);
    renderer_.fillRect(12,60,351,246,false);
    books::paintFrame(display_,10,58,355,250,2);
    renderer_.drawText(2,20,70,books::tr("Footnote"));
    if(note.empty())note="Preview unavailable.";
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
    books::paintFrame(display_,10,38,355,258,2);
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
    display_.displayBuffer();
  }
  const std::vector<books::Highlight>& highlights() {
    if(!highlightsLoaded_) {
      highlights_=books::loadHighlights(bookPath_);
      highlightsLoaded_=true;
    }
    return highlights_;
  }
  bool beginTextSelection() {
    if(!upstreamSection_)return false;
    auto page=upstreamSection_->loadPage(static_cast<int>(currentPage_));
    if(!page)return false;
    selectedWords_=books::selectableWords(
        *page,renderer_,upstreamFontId_,preferences_.margin,TOP);
    selectionAnchor_=selectedWords_.empty()?-1:0;
    selectionFocus_=selectionAnchor_;
    return selectionAnchor_>=0;
  }
  bool selectTouchWord(int x,int y,bool extend) {
    if(!extend || selectedWords_.empty())
      if(!beginTextSelection())return false;
    const int picked=books::hitSelectableWord(selectedWords_,x,y);
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
       !books::addHighlight(bookPath_,{
         static_cast<uint32_t>(currentSpine_),start,end}))return false;
    // Keep the words for a clippings export (offsets alone can't be read back).
    (void)books::rememberClipText(bookPath_,{static_cast<uint32_t>(currentSpine_),start,end,selectedText()});
    highlightsLoaded_=false;
    return true;
  }
  bool eraseSelectionHighlight() {
    uint32_t start=0,end=0;
    if(!selectionRange(start,end) ||
       !books::removeHighlightAt(bookPath_,
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
    const auto words=books::selectableWords(
        *page,renderer_,upstreamFontId_,preferences_.margin,TOP);
    uint32_t start=0,end=0;
    const bool active=selectionRange(start,end);
    for(const auto& word:words) {
      bool marked=active &&
          books::overlapsVisibleWord(word,start,end);
      if(!marked)
        for(const auto& h:highlights)
          if(h.spine==static_cast<uint32_t>(currentSpine_) &&
             books::overlapsVisibleWord(word,h.begin,h.end)) {
            marked=true;break;
          }
      if(marked)books::paintHighlight(display_,word.x-1,word.y+2,word.width+2,
                              std::max(8,word.height-7));
    }
  }
  void showSelectionPreview(bool extending=false) {
    render();
    renderer_.fillRect(10,254,355,47,true);
    renderer_.fillRect(12,256,351,43,false);
    books::paintFrame(display_,10,254,355,47,2);
    drawMenuText(20,265,extending?"Selecting text":"Choose a word");
    display_.displayBuffer();
  }
  void showTextSelectionMenu(int option) {
    render();
    renderer_.fillRect(10,207,355,96,true);
    renderer_.fillRect(12,209,351,92,false);
    books::paintFrame(display_,10,207,355,96,2);
    renderer_.drawText(2,20,215,books::tr("Select text"));
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
  // DI-03: every match from every dictionary, paged with the wheel. Pages
  // are laid out once per lookup; the overlay shows one at a time.
  void showDictionaryDefinition(int step=0) {
    const auto selected=selectedText();
    if(step==0 || selected!=dictionaryWord_) {
      dictionaryWord_=selected;
      dictionaryPages_.clear();
      dictionaryPage_=0;
      const auto matches=books::lookupDefinitions(selected);
      if(!matches.empty())  // DI-04: the words looked up, for review later
        (void)books::logVocabulary(selected,matches.front().headword,metadata().coreMetadata.title);
      constexpr int kLines=4;
      constexpr int kWidth=325;  // inside the overlay, in the overlay font
      const auto fits=[&](const std::string& t){return renderer_.getTextWidth(2,t.c_str())<=kWidth;};
      for(size_t m=0;m<matches.size();++m) {
        const std::string header=matches[m].headword+"  ("+matches[m].source+")";
        std::vector<std::string> lines;
        const std::string& text=matches[m].text;
        size_t cursor=0;
        while(cursor<text.size() && lines.size()<200) {
          // The longest run of whole words that fits; a word wider than the
          // overlay is cut at a character boundary.
          size_t end=cursor,lastSpace=std::string::npos;
          while(end<text.size()) {
            size_t next=end+1;
            while(next<text.size() && (static_cast<unsigned char>(text[next])&0xC0)==0x80)++next;
            if(!fits(text.substr(cursor,next-cursor)))break;
            if(text[end]==' ')lastSpace=end;
            end=next;
          }
          if(end<text.size() && lastSpace!=std::string::npos && lastSpace>cursor)end=lastSpace;
          if(end==cursor) {  // not even one character fits: take one
            end=cursor+1;
            while(end<text.size() && (static_cast<unsigned char>(text[end])&0xC0)==0x80)++end;
          }
          lines.push_back(text.substr(cursor,end-cursor));
          cursor=end;
          while(cursor<text.size() && text[cursor]==' ')++cursor;
        }
        for(size_t l=0;l<lines.size() || l==0;l+=kLines) {
          DictionaryPage page{header,{}};
          for(size_t k=l;k<std::min(lines.size(),l+kLines);++k)page.lines.push_back(lines[k]);
          dictionaryPages_.push_back(std::move(page));
          if(lines.empty())break;
        }
      }
    } else {
      dictionaryPage_=std::clamp(dictionaryPage_+step,0,
          std::max(0,static_cast<int>(dictionaryPages_.size())-1));
    }
    render();
    renderer_.fillRect(10,90,355,195,true);
    renderer_.fillRect(12,92,351,191,false);
    books::paintFrame(display_,10,90,355,195,2);
    renderer_.drawText(2,20,103,books::tr("Offline dictionary"));
    if(dictionaryPages_.empty()) {
      renderer_.drawText(2,20,133,clippedLabel(selected,32).c_str());
      renderer_.drawText(2,20,158,books::tr("No match in the dictionaries"));
    } else {
      const auto& page=dictionaryPages_[dictionaryPage_];
      std::string header=page.header;
      while(header.size()>1 && renderer_.getTextWidth(2,header.c_str())>325) {
        size_t cut=header.size()-1;
        while(cut>0 && (static_cast<unsigned char>(header[cut])&0xC0)==0x80)--cut;
        header.erase(cut);
      }
      renderer_.drawText(2,20,131,header.c_str());
      renderer_.fillRect(20,159,320,1,true);
      for(size_t line=0;line<page.lines.size();++line)
        renderer_.drawText(2,20,163+static_cast<int>(line)*25,page.lines[line].c_str());
      if(dictionaryPages_.size()>1) {  // which page of the definition
        const std::string footer=std::to_string(dictionaryPage_+1)+"/"+
            std::to_string(dictionaryPages_.size());
        drawMenuText(20,271,footer.c_str(),1);
      }
    }
    display_.displayBuffer();
  }
  bool hasMoreDictionaryPages() const {return dictionaryPages_.size()>1;}
  bool rememberPage() {
    const uint32_t offset=upstreamSection_?
      upstreamSection_->getVisibleTextOffsetForPage(
        static_cast<uint16_t>(currentPage_)).value_or(0):0;
    const bool saved=books::addBookmark(bookPath_,
      {currentSpine_,currentPage_,offset});
    typographyNotice_=saved?"Bookmark saved":"Cannot save bookmark";
    return saved;
  }
  std::vector<books::Bookmark> bookmarks() const {
    return books::loadBookmarks(bookPath_);
  }
  bool goToBookmark(const books::Bookmark& mark) {
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
  // Chapter/bookmark list: Q2 Pod's list layout (MenuLayout). The names use
  // the reading font, so chapters in any script show.
  int navCount(bool showingBookmarks) const {
    return static_cast<int>(showingBookmarks?bookmarks().size():chapterCount());
  }
  // The item tapped at y, or -1 off the rows.
  int navRowAt(bool showingBookmarks,int selected,int y) const {
    const int count=navCount(showingBookmarks);
    return books::MenuLayout::itemAt(books::MenuLayout::first(selected,count),count,y);
  }
  void showNavigationMenu(bool showingBookmarks,int selected) {
    using L=books::MenuLayout;
    paintListBar(showingBookmarks?"Bookmarks":"Chapters");
    const auto marks=showingBookmarks?bookmarks():
      std::vector<books::Bookmark>{};
    const size_t count=showingBookmarks?marks.size():chapterCount();
    if(!count)renderer_.drawText(1,L::kTextX,L::textTop(L::rowY(0),renderer_.getLineHeight(1)),
                                 books::tr(showingBookmarks?"No bookmarks yet":"No chapters"));
    const int height=renderer_.getLineHeight(1);
    const int start=L::first(selected,static_cast<int>(count));
    for(int row=0;row<L::kRows && start+row<static_cast<int>(count);++row) {
      const int item=start+row;
      const int y=L::rowY(row);
      std::string label;
      if(showingBookmarks) {
        const auto& mark=marks[item];
        label="Ch "+std::to_string(mark.spine+1)+
          " - Page "+std::to_string(mark.page+1);
      } else label=std::to_string(item+1)+". "+chapterName(item);
      renderer_.drawText(1,L::kTextX,L::textTop(y,height),
          renderer_.truncatedText(1,label.c_str(),L::kValueRight-L::kTextX).c_str());
      if(item==selected)invertMenuRect(0,y,L::kWidth,L::kRowH);
    }

    display_.displayBuffer();
  }

  bool adjustPreference(int option) {
    books::ReaderPreferences next=preferences_;
    switch(option) {
      case 2: next.linePercent=next.linePercent==100?120:
          (next.linePercent==120?140:100);break;
      case 3: next.margin=next.margin==14?22:
          (next.margin==22?30:14);break;
      case 4: next.alignment=1-next.alignment;break;
      case 5: next.hyphenation=!next.hyphenation;break;
      case 6: next.embeddedStyle=!next.embeddedStyle;break;
      case 7: books::toggleNight(next,systemDark());break;
      case 16: next.focusReading=!next.focusReading;break;
      case 17: {
        const auto& choices=books::autoTurnChoices;
        const auto* at=std::find(std::begin(choices),std::end(choices),next.autoTurnSeconds);
        next.autoTurnSeconds=(at==std::end(choices)||at+1==std::end(choices))?choices[0]:*(at+1);
        break;
      }
      default:return false;
    }
    if(!books::validReaderPreferences(next))return false;
    if(option==17) {
      // A timer, not layout: nothing to re-lay out or invalidate.
      preferences_=next;
      const bool saved=books::saveReaderPreferences(preferences_);
      typographyNotice_=!saved?"Reading settings not saved":
          next.autoTurnSeconds?"Over 2 min idle needs Q2 Pod Smart Awake":"";
      return true;
    }
    if(option==7) {
      // Inversion is a framebuffer setting; no new Section layout and no
      // SD cache invalidation are necessary.
      preferences_=next;
      clearRasterCache();
      const bool saved=books::saveReaderPreferences(preferences_);
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
    if(!books::saveReaderPreferences(preferences_))
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
  // Menu text in the reading font's own glyphs, scaled to the bitmap menu
  // font's size: used for translated menus, which need accents the 5x8
  // bitmap font has not. Stops at a whole character before maxX.
  // Menu text sizes in Q2 Pod's px (UiText), as its themes set them: list
  // rows 27 in Classic's face and 24 in the others (scale 2), small text
  // (scale 1), large figures (scale 3); the bar is kBarPx in every theme.
  static constexpr int kBarPx=19;
  static int uiPx(int scale) {
    const bool classic=books::uiTheme().style==books::UiTheme::Style::Classic;
    return scale>=3?38:scale==2?(classic?25:22):(classic?16:13);
  }
  // Q2 Pod's TrueType faces, smooth and fixed in size. The capitals sit where
  // the old glyph box did, so callers' coordinates keep working. False when
  // UiText is off or lacks a character (the fallback draws it).
  bool drawUiText(int x,int top,const std::string& text,int scale,int maxX,int px=0) {
    auto& ui=books::UiText::instance();
    if(!ui.available() || !ui.drawable(text))return false;
    if(px<=0)px=uiPx(scale);
    const int box=scale>=2?16:9,cap=px*7/10;
    ui.draw(display_,x,top+(box+cap)/2-ui.ascent(px),text,px,maxX);
    return true;
  }
  // Fallback glyphs: the fixed built-in UI font (slot 2), never the reading
  // font, so changing the book's font or size leaves the menus alone.
  static constexpr int kUiFontSlot=2;
  void drawGlyphMenuText(int x,int top,const std::string& text,int scale,int maxX) {
    const int scale256=scale>=2?180:102;
    const int baseline=top+(scale>=2?13:8);
    const auto* p=reinterpret_cast<const unsigned char*>(text.c_str());
    while(*p) {
      const uint32_t cp=utf8NextCodepoint(&p);
      int32_t advance=0;int topBearing=0;
      if(!renderer_.getCodepointMetrics(kUiFontSlot,cp,EpdFontFamily::REGULAR,advance,topBearing))continue;
      if(x+fp4::toPixel(advance*scale256/256)>maxX)break;
      x+=renderer_.drawScaledCodepoint(kUiFontSlot,cp,EpdFontFamily::REGULAR,x,baseline,scale256);
    }
  }
  // The width drawMenuText gives |str| at |scale| (pixel font or glyphs).
  int menuTextWidth(const char* str,int scale) {
    {
      auto& ui=books::UiText::instance();
      const std::string text=books::tr(str);
      if(ui.available() && ui.drawable(text))return ui.width(text,uiPx(scale));
    }
    if(!books::uiTranslated() && !books::uiTheme().active())
      return static_cast<int>(std::strlen(str))*6*scale;
    const std::string text=books::tr(str);
    const int scale256=scale>=2?180:102;
    int width=0;
    const auto* p=reinterpret_cast<const unsigned char*>(text.c_str());
    while(*p) {
      const uint32_t cp=utf8NextCodepoint(&p);
      int32_t advance=0;int topBearing=0;
      if(renderer_.getCodepointMetrics(kUiFontSlot,cp,EpdFontFamily::REGULAR,advance,topBearing))
        width+=fp4::toPixel(advance*scale256/256);
    }
    return width;
  }
  void drawMenuText(int x,int top,const char* str,int scale=2,int maxX=352) {
    if(drawUiText(x,top,books::tr(str),scale,maxX))return;
    // Translated menus need accents; Q2 Pod's themes draw smooth text: both
    // use the UI font's glyphs instead of the pixel font.
    if(books::uiTranslated() || books::uiTheme().active()) {
      drawGlyphMenuText(x,top,books::tr(str),scale,maxX);
      return;
    }
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
    {0x18,0xa4,0xa4,0xa4,0x7c}, // 103 (descender in row 7)
    {0x7f,0x04,0x04,0x04,0x78}, // 104
    {0x00,0x44,0x7d,0x40,0x00}, // 105
    {0x40,0x80,0x84,0x7d,0x00}, // 106 (descender in row 7)
    {0x7f,0x10,0x28,0x44,0x00}, // 107
    {0x00,0x41,0x7f,0x40,0x00}, // 108
    {0x7c,0x04,0x78,0x04,0x78}, // 109
    {0x7c,0x04,0x04,0x04,0x78}, // 110
    {0x38,0x44,0x44,0x44,0x38}, // 111
    {0xfc,0x24,0x24,0x24,0x18}, // 112 (descender in row 7)
    {0x18,0x24,0x24,0x18,0xfc}, // 113 (descender in row 7)
    {0x7c,0x08,0x04,0x04,0x08}, // 114
    {0x48,0x54,0x54,0x54,0x24}, // 115
    {0x04,0x3f,0x44,0x40,0x20}, // 116
    {0x3c,0x40,0x40,0x20,0x7c}, // 117
    {0x1c,0x20,0x40,0x20,0x1c}, // 118
    {0x3c,0x40,0x30,0x40,0x3c}, // 119
    {0x44,0x28,0x10,0x28,0x44}, // 120
    {0x1c,0xa0,0xa0,0xa0,0x7c}, // 121 (descender in row 7)
    {0x44,0x64,0x54,0x4c,0x44}, // 122
    {0x00,0x08,0x36,0x41,0x00}, // 123
    {0x00,0x00,0x7f,0x00,0x00}, // 124
    {0x00,0x41,0x36,0x08,0x00}, // 125
    {0x04,0x02,0x04,0x04,0x02}, // 126
    };
    for(const unsigned char* p=
          reinterpret_cast<const unsigned char*>(str);*p;++p) {
      const unsigned cp=*p>=32 && *p<=126?*p:static_cast<unsigned>('?');
      if(x+6*scale>maxX)break;
      const uint8_t* glyph=glyphs[cp-32];
      // 8 rows: g j p q y descend into row 7 (in 7 rows "g" read as "9").
      for(int gx=0;gx<5;++gx)
        for(int gy=0;gy<8;++gy)
          if(glyph[gx] & (1u<<gy))
            renderer_.fillRect(x+gx*scale,top+gy*scale,scale,scale,true);
      x+=6*scale;
    }
  }
  // The chosen row of a menu: Q2 Pod's selection when a theme is set,
  // else inverted.
  void invertMenuRect(int x,int y,int w,int h) {books::paintSelection(display_,x,y,w,h);}
  // First visible row of the settings list for a selected row.
  static int menuFirstRow(int selectedRow,int count) {
    return books::MenuLayout::first(selectedRow,count);
  }
  // |items|: the menu items to list, in order (default: all of them). The
  // Books handoff reader lists only the rows it can act on.
  void showTypographyMenu(int selected,const int* items=nullptr,int count=0) {
    // Q2 Pod's list layout (MenuLayout); the page is not rendered under it.
    using L=books::MenuLayout;
    constexpr int kItems=kTypographyItems;
    std::vector<int> rows;
    if(items && count>0)rows.assign(items,items+count);
    else for(int i=0;i<kItems;++i)rows.push_back(i);
    const int listed=static_cast<int>(rows.size());
    const int selectedRow=static_cast<int>(
        std::find(rows.begin(),rows.end(),selected)-rows.begin());
    const int first=menuFirstRow(std::min(selectedRow,listed-1),listed);
    paintListBar("Reading settings");
    const char* labels[]={"Font","Size","Line spacing","Margins","Alignment",
        "Hyphenation","Book CSS","Night mode","Add bookmark","Bookmarks","Chapters",
        "Page links","Return link","Select text","Sync: upload","Sync: download",
        "Focus read","Auto turn","Go to %","About","Dictionaries","Export notes"};
    const std::string values[]={
      books::typographyFamilyName(typography_.family),
      std::to_string(typography_.size)+"pt",
      std::to_string(preferences_.linePercent)+"%",
      std::to_string(preferences_.margin)+"px",
      preferences_.alignment?"Justify":"Book default",
      preferences_.hyphenation?"On":"Off",
      preferences_.embeddedStyle?"On":"Off",
      nightShown()?"On":"Off",
      "Save page","Saved pages","Browse book","Footnotes / links",
      hasLinkHistory()?"Go back":"No previous link","Highlight / define",
      "Save position","Use remote position",
      preferences_.focusReading?"On":"Off",
      preferences_.autoTurnSeconds?std::to_string(preferences_.autoTurnSeconds)+"s":"Off",
      std::to_string(bookPercent())+"%",
      std::string("v")+BOOKS_VERSION,
      std::to_string(dictionaryCount())+" "+books::tr("installed"),
      "To Clippings"
    };
    static_assert(sizeof(labels)/sizeof(labels[0])==kItems,"one label per row");
    static_assert(sizeof(values)/sizeof(values[0])==kItems,"one value per row");
    for(int row=0;row<L::kRows && first+row<listed;++row) {
      const int item=rows[first+row];
      const int y=L::rowY(row);
      const bool active=selected==item;
      // Invert the completed row, including the glyph pixels, after drawing.
      // Separate columns prevent long values from colliding with labels.
      // English: the bitmap font, cut by characters; translated: the reading
      // font's glyphs, cut by width at the value column.
      const bool english=!books::uiTranslated() && !books::uiTheme().active();
      const std::string label=english?clippedLabel(labels[item],12):books::tr(labels[item]);
      const std::string value=english?clippedLabel(values[item],12):books::tr(values[item]);
      // The value sits on the right, as Q2 Pod's rows; a long one starts
      // just after its label and is cut at the right edge.
      const int labelEnd=L::kTextX+menuTextWidth(label.c_str(),2);
      const int valueX=std::max(labelEnd+12,L::kValueRight-menuTextWidth(value.c_str(),2));
      drawMenuText(L::kTextX,y+8,label.c_str(),2,valueX-8);
      drawMenuText(valueX,y+8,value.c_str(),2,L::kValueRight+4);
      if(active)invertMenuRect(0,y,L::kWidth,L::kRowH);
    }
    if(!typographyNotice_.empty())paintListNotice(books::tr(typographyNotice_));
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
          std::printf("Books original Section: spine %d/%d, %zu page(s)%s\n",
              index+1,n,pageCount(),
              upstreamSection_->isBuilding()?" (incremental)":"");
          return true;
        }
        std::fprintf(stderr,
           "Books: original Section could not lay out spine %d; falling back\n",index+1);
      }
      const auto entry=metadata().getSpineEntry(index);
      std::vector<Paragraph> paragraphs;
      if(parseChapter(archive_,packagePath_,entry.href,paragraphs) &&
         paginate(paragraphs)) {
        upstreamSection_.reset();
        currentSpine_=index;
        currentPage_=direction<0?pages_.size()-1:0;
        std::printf("Books legacy fallback: spine %d/%d, %zu page(s)\n",
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
      std::fprintf(stderr,"Books: incremental Section failed; preserving book via fallback\n");
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
      std::printf("Books Section completed: %zu pages\n",pageCount());
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
  bool atBookEnd() const {
    return currentSpine_+1>=metadata().getSpineCount() && !isBuilding() &&
           currentPage_+1>=pageCount();
  }
  bool move(int direction) {
    if(direction>0 && currentPage_+1>=pageCount() && isBuilding()) {
      // Lay out until the next page exists, for up to 2 s: a press that came
      // before the prefetch was dropped, so fast turns lost pages. Never
      // skip unread content: past the bound the turn waits for the next press.
      const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(2);
      do (void)buildNextPages(2);
      while(isBuilding() && currentPage_+1>=pageCount() && !interrupted &&
            std::chrono::steady_clock::now()<until);
      if(isBuilding() && currentPage_+1>=pageCount())return false;
    }
    if (direction>0 && currentPage_+1<pageCount()) { ++currentPage_; persistPosition(); return true; }
    if (direction<0 && currentPage_>0) { --currentPage_; persistPosition(); return true; }
    const bool changed=loadSpine(currentSpine_+direction,direction);
    if(changed)persistPosition();
    return changed;
  }
  // Q2 Pod's theme is dark when its page colour is: Modern, Minimal, or
  // Classic with Light Mode off. No theme reads as light.
  static bool systemDark() {
    const auto& t=books::uiTheme();
    if(!t.active())return false;
    const uint32_t c=t.box;
    return (((c>>16)&255u)*299u+((c>>8)&255u)*587u+(c&255u)*114u)/1000u<128u;
  }
  bool nightShown() const {return books::nightActive(preferences_,systemDark());}
  // Page colours. Night mode is a dark page whatever the theme: the theme's
  // own colours when they match, else its ink and paper swapped. The page is
  // never inverted, so smooth text and the theme's zones stay as drawn.
  void applyPagePalette() {
    const auto& t=books::uiTheme();
    if(t.active()) {
      if(nightShown()!=systemDark())display_.setPalette(t.box,t.title);
      else books::applyThemePalette(display_);
      display_.setInverted(false);
    } else display_.setInverted(nightShown());
  }
  // Menus and dialogs: always the system's theme colours.
  void applyMenuPalette() {
    books::applyThemePalette(display_);
    display_.setInverted(false);
  }
  // Book progress in parts per million, weighted by chapter size as upstream
  // CrossPoint's Epub::calculateProgress is: the chapters' cumulative XHTML
  // sizes, plus the place within this chapter (the saved-position fraction).
  uint32_t bookProgressPpm() const {
    const int spines=metadata().getSpineCount();
    if(spines<=0 || currentSpine_<0)return 0;
    if(atBookEnd())return 1000000u;  // the last page reads 100 %, not 99
    const uint64_t total=metadata().getCumulativeSize(spines-1);
    const uint64_t before=currentSpine_>0?metadata().getCumulativeSize(currentSpine_-1):0;
    const uint64_t here=metadata().getCumulativeSize(currentSpine_);
    if(total==0) {  // no sizes: chapters weigh the same
      return static_cast<uint32_t>((static_cast<uint64_t>(currentSpine_)*1000000u+
          currentPosition().fractionPpm)/spines);
    }
    const uint64_t at=before+(here-before)*currentPosition().fractionPpm/1000000u;
    return static_cast<uint32_t>(std::min<uint64_t>(1000000u,at*1000000u/total));
  }
  int bookPercent() const { return static_cast<int>(bookProgressPpm()/10000u); }
  const std::string& notice() const {return typographyNotice_;}
  // RD-01: open the page at a book position (0-100 %). The chapter is the one
  // holding that share of the book's text, and the page is placed within it
  // the same way a saved position is carried across a layout change.
  bool goToBookPercent(int percent) {
    percent=std::clamp(percent,0,100);
    const int spines=metadata().getSpineCount();
    if(spines<=0)return false;
    const uint64_t total=metadata().getCumulativeSize(spines-1);
    int spine=0;uint32_t within=0;
    if(total==0) {
      const uint64_t scaled=static_cast<uint64_t>(percent)*spines*10000u;  // ppm of spines
      spine=static_cast<int>(std::min<uint64_t>(spines-1,scaled/1000000u));
      within=static_cast<uint32_t>(scaled-static_cast<uint64_t>(spine)*1000000u);
    } else {
      const uint64_t target=total*static_cast<uint64_t>(percent)/100u;
      while(spine<spines-1 && metadata().getCumulativeSize(spine)<=target)++spine;
      const uint64_t before=spine>0?metadata().getCumulativeSize(spine-1):0;
      const uint64_t size=metadata().getCumulativeSize(spine)-before;
      within=size?static_cast<uint32_t>(std::min<uint64_t>(999999u,(target-before)*1000000u/size)):0;
    }
    if(percent==100) {  // the book's last page, not the last chapter's first
      if(!loadSpine(spines-1,-1))return false;
      persistPosition(true);
      return true;
    }
    if(!loadSpine(spine,1) || currentSpine_!=spine)return false;
    books::ReadingPosition place{spine,0,upstreamSection_?'l':'o',0,within};
    placeSavedPosition(place);
    clearRasterCache();
    persistPosition(true);
    return true;
  }
  // Reading settings -> Dictionaries: each installed dictionary, on or off.
  void showDictionaryList(int selected) {
    using L=books::MenuLayout;
    const auto names=books::dictionaryNames();
    const auto off=books::disabledDictionaries();
    paintListBar("Dictionaries");
    if(names.empty()) {
      drawMenuText(L::kTextX,L::rowY(0)+8,"None installed.",1);
      drawMenuText(L::kTextX,L::rowY(0)+24,"Put StarDict folders in",1);
      drawMenuText(L::kTextX,L::rowY(0)+38,".books/dictionaries or Dictionaries",1);
    }
    const int count=static_cast<int>(names.size());
    const int first=L::first(selected,count);
    for(int row=0;row<L::kRows && first+row<count;++row) {
      const int item=first+row,y=L::rowY(row);
      const bool on=std::find(off.begin(),off.end(),names[item])==off.end();
      const char* value=on?"On":"Off";
      const int valueX=L::kValueRight-menuTextWidth(value,2);
      drawMenuText(L::kTextX,y+8,clippedLabel(names[item],22).c_str(),2,valueX-8);
      drawMenuText(valueX,y+8,value,2,L::kValueRight+4);
      if(item==selected)invertMenuRect(0,y,L::kWidth,L::kRowH);
    }
    display_.displayBuffer();
  }
  // Toggle dictionary |index| of dictionaryNames(); false if none.
  bool toggleDictionary(int index) {
    const auto names=books::dictionaryNames();
    if(index<0 || index>=static_cast<int>(names.size()))return false;
    auto off=books::disabledDictionaries();
    const auto at=std::find(off.begin(),off.end(),names[index]);
    if(at==off.end())off.push_back(names[index]);else off.erase(at);
    return books::saveDisabledDictionaries(off);
  }
  // M3: highlights and bookmarks to Clippings/<title>.txt on the card.
  bool exportNotes() {
    const auto& info=metadata().coreMetadata;
    std::string written;int count=0;
    const bool ok=books::exportClippings(bookPath_,info.title,info.author,[&](int spine) {
      std::string name;
      if(upstreamEpub_ && upstreamEpub_->getTocItemsCount()>0) {
        const int toc=upstreamEpub_->getTocIndexForSpineIndex(spine);
        if(toc>=0)name=chapterName(static_cast<size_t>(toc));
      }
      return name.empty()?"Chapter "+std::to_string(spine+1):name;
    },written,count);
    typographyNotice_=ok?"Saved "+std::to_string(count)+" notes to "+written.substr(written.find("Clippings")):
        count==0?"No highlights or bookmarks yet":"Cannot write Clippings";
    return ok;
  }
  int dictionaryCount() const {return static_cast<int>(books::dictionaryNames().size());}
  void showAbout() {
    renderer_.clearScreen(0xff);
    applyMenuPalette();
    renderer_.fillRect(10,37,355,258,true);
    renderer_.fillRect(12,39,351,254,false);
    books::paintFrame(display_,10,37,355,258,2);
    drawMenuText(22,47,(std::string("Books ")+BOOKS_VERSION).c_str());
    renderer_.fillRect(18,66,337,1,true);
    if(books::uiTheme().style==books::UiTheme::Style::Classic)books::paintBar(display_,12,39,351,27);
    int y=76;
    for(const char* line:books::kAboutLines) {
      const bool small=!line[0] || std::strncmp(line,"github",6)==0 || std::strncmp(line,"MIT",3)==0;
      drawMenuText(22,y+(small&&line[0]?4:0),line,small?1:2);
      y+=!line[0]?8:19;
    }
    display_.displayBuffer();
  }
  void showPercentDialog(int percent) {
    renderer_.clearScreen(0xff);
    applyMenuPalette();
    renderer_.fillRect(10,70,355,170,true);
    renderer_.fillRect(12,72,351,166,false);
    books::paintFrame(display_,10,70,355,170,2);
    drawMenuText(22,82,"Go to percent");
    renderer_.fillRect(18,101,337,1,true);
    const std::string value=std::to_string(std::clamp(percent,0,100))+"%";
    drawMenuText(187-static_cast<int>(value.size())*9,130,value.c_str(),3);
    renderer_.drawLine(30,180,345,180,true);
    renderer_.fillRect(30,177,static_cast<int>(3.15f*std::clamp(percent,0,100)),7,true);
    display_.displayBuffer();
  }
  // RD-06: page within the chapter (an estimate while it is still laid out)
  // and the book percentage, beside a byte-weighted progress bar.
  void drawProgress() {
    const int totalChapters=metadata().getSpineCount();
    if(totalChapters<=0 || currentSpine_<0)return;
    using L=books::MenuLayout;
    const size_t pages=std::max<size_t>(1,estimatedPages());
    const std::string label=std::to_string(currentPage_+1)+"/"+
        (isBuilding()?"~":"")+std::to_string(pages)+"  "+std::to_string(bookPercent())+"%";
    status_=books::readDeviceStatus();
    // Top band, as Q2 Pod's bar: the book on the left, the clock centred, the
    // battery on the right. Bottom band: the progress bar and the page. Both
    // keep clear of the Q2's rounded corners (MenuLayout::cornerX).
    constexpr int kTopText=16,kBottomText=290,kTextH=10,kIconW=16;
    renderer_.fillRect(0,0,375,TOP,false);
    renderer_.fillRect(0,BOTTOM,375,SCREEN_H-BOTTOM,false);
    const int topInset=L::cornerX(kTopText,kTextH);
    const std::string clock=books::clockLabel(status_);
    const std::string battery=books::batteryLabel(status_);
    const int clockW=menuTextWidth(clock.c_str(),1);
    const int clockX=(375-clockW)/2;
    drawMenuText(clockX,kTopText,clock.c_str(),1,clockX+clockW+4);
    if(!battery.empty()) {
      const int iconX=375-topInset-kIconW;
      drawBatteryIcon(iconX,kTopText,status_);
      const int textX=iconX-4-menuTextWidth(battery.c_str(),1);
      drawMenuText(textX,kTopText,battery.c_str(),1,iconX);
    }
    // The book's title only when the UI faces have all its letters (a title
    // in a script they lack is left out rather than drawn as boxes).
    const std::string title=std::string(metadata().coreMetadata.title);
    if(!title.empty() && books::UiText::instance().drawable(title))
      drawMenuText(topInset,kTopText,title.c_str(),1,clockX-12);
    const int bottomInset=L::cornerX(kBottomText,kTextH);
    const int labelX=375-bottomInset-menuTextWidth(label.c_str(),1);
    const int barStart=bottomInset;
    const int barEnd=std::max(barStart+20,labelX-10);
    const float progress=bookProgressPpm()/1000000.0f;
    const int filled=static_cast<int>(progress*static_cast<float>(barEnd-barStart));
    renderer_.drawLine(barStart,kBottomText+5,barEnd,kBottomText+5,true);
    if(filled>0)renderer_.fillRect(barStart,kBottomText+3,filled,3,true);
    drawMenuText(labelX,kBottomText,label.c_str(),1,375-bottomInset+4);
  }
  // A 16x9 battery: body, tip, fill, and a bolt while charging (each bolt
  // pixel the opposite of what is under it, so it shows on fill or not).
  void drawBatteryIcon(int x,int y,const books::DeviceStatus& status) {
    renderer_.drawRect(x,y,14,9,true);
    renderer_.fillRect(x+14,y+2,2,5,true);
    const int fill=books::batteryFill(status.battery,10);
    if(fill>0)renderer_.fillRect(x+2,y+2,fill,5,true);
    if(status.charging) {
      static constexpr int8_t kBolt[][2]={{8,1},{7,2},{6,3},{5,4},{6,4},{7,4},{8,4},
                                          {9,4},{8,5},{7,6},{6,7}};
      for(const auto& p:kBolt)renderer_.drawPixel(x+p[0],y+p[1],p[0]>=2+fill);
    }
  }
  // Q2 Pod's list page (MenuLayout): a clear screen and the bar, with the
  // title on the left and the clock and battery on the right. Q2 Pod centres
  // its clock, but makes way for a title; Books' titles need the room.
  void paintListBar(const char* title) {
    using L=books::MenuLayout;
    renderer_.clearScreen(0xff);
    applyMenuPalette();
    const auto status=books::readDeviceStatus();
    const std::string clock=books::clockLabel(status);
    // Text 8 to 24 px down the bar: kept clear of the rounded corners.
    const int inset=L::cornerX(8,16);
    int right=L::kWidth-inset;
    if(status.battery>=0) {
      right-=16;
      drawBatteryIcon(right,12,status);
      right-=8;
    }
    // Q2 Pod's bar size where the UI faces are there, else the row font.
    auto& ui=books::UiText::instance();
    const std::string name=books::tr(title);
    const int clockX=right-(ui.available()?ui.width(clock,kBarPx):menuTextWidth(clock.c_str(),2));
    if(!drawUiText(clockX,8,clock,2,right+4,kBarPx))drawMenuText(clockX,8,clock.c_str(),2,right+4);
    if(!drawUiText(inset,8,name,2,clockX-10,kBarPx))drawMenuText(inset,8,title,2,clockX-10);
    books::paintBar(display_,0,0,L::kWidth,L::kBarH);
  }
  // A notice under the rows (a setting's result); no control hints: the
  // wheel, Centre and Back work as everywhere on the Q2. Kept clear of the
  // rounded bottom corners.
  void paintListNotice(const std::string& notice) {
    using L=books::MenuLayout;
    if(notice.empty())return;
    const int top=L::kFooterY+8,inset=L::cornerX(top,12);
    drawMenuText(inset,top,clippedLabel(notice,56).c_str(),1,L::kWidth-inset);
  }

  // True when the clock's minute or the battery changed since the footer was
  // drawn: the idle loop then presents the page again.
  bool statusChanged() const {return books::readDeviceStatus()!=status_;}
  void cacheRenderedPage() {
    const uint8_t* framebuffer=display_.getFrameBuffer();
    if(!framebuffer || currentSpine_<0)return;
    auto& slot=rasterCache_[nextRasterSlot_++%rasterCache_.size()];
    slot.spine=currentSpine_;
    slot.page=currentPage_;
    slot.pixels.assign(framebuffer,framebuffer+HalDisplay::BUFFER_SIZE);
    slot.alpha.assign(display_.coverageAlpha(),display_.coverageAlpha()+HalDisplay::PIXELS);
    slot.expect.assign(display_.coverageExpect(),display_.coverageExpect()+HalDisplay::PIXELS);
  }
  // Smooth text: renders the page twice more in the engine's grayscale modes
  // (as upstream CrossPoint does for e-ink anti-aliasing) and keeps the two
  // planes, so the LCD can show each glyph edge at its true coverage instead
  // of solid ink. The BW page is put back unchanged. BOOKS_SMOOTH_TEXT=0
  // turns it off.
  template<typename PagePtr>
  void captureGrayPlanes(const PagePtr& page) {
    static const bool enabled=[]{
      const char* v=std::getenv("BOOKS_SMOOTH_TEXT");
      return !(v && std::strcmp(v,"0")==0);
    }();
    uint8_t* framebuffer=display_.getFrameBuffer();
    if(!enabled || !framebuffer)return;
    std::vector<uint8_t> bw(framebuffer,framebuffer+HalDisplay::BUFFER_SIZE);
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
    page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
    renderer_.copyGrayscaleLsbBuffers();
    renderer_.clearScreen(0x00);
    renderer_.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
    page->render(renderer_,upstreamFontId_,preferences_.margin,TOP);
    renderer_.copyGrayscaleMsbBuffers();
    renderer_.setRenderMode(GfxRenderer::BW);
    std::memcpy(framebuffer,bw.data(),bw.size());
    display_.setGrayPage(framebuffer);
    drawSmoothGlyphs(page);
  }
  void render() {
    display_.clearZones();  // a cached page is copied, not cleared
    uint8_t* framebuffer=display_.getFrameBuffer();
    constexpr size_t kBytes=HalDisplay::BUFFER_SIZE;
    if(framebuffer) {
      for(const auto& cached:rasterCache_)
        if(cached.spine==currentSpine_ && cached.page==currentPage_ &&
           cached.pixels.size()==kBytes) {
          std::memcpy(framebuffer,cached.pixels.data(),kBytes);
          if(cached.alpha.size()==HalDisplay::PIXELS && cached.expect.size()==HalDisplay::PIXELS)
            display_.restoreCoverage(cached.alpha.data(),cached.expect.data());
          else display_.clearCoverage();
          lastRenderOk_=true;
          drawProgress();
          applyPagePalette();
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
            "Books: page %zu in spine %d unavailable; refusing blank cache\n",
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
      if(lastRenderOk_)captureGrayPlanes(page);
      if(lastRenderOk_)cacheRenderedPage();
      else {
        // Direct bitmap menu glyphs work even when the SD-book font has
        // failed; leave a visible diagnostic instead of a silent white page.
        drawMenuText(20,140,"Page text unavailable",1);
        std::fprintf(stderr,"Books: no body ink after glyph retry at spine %d page %zu\n",
                     currentSpine_,currentPage_);
      }
      drawProgress();
      applyPagePalette();
      paintHighlightOverlay();
      return;
    }
    if(currentPage_>=pages_.size()) {
      std::fputs("Books: legacy page unavailable; preserving current framebuffer\n",stderr);
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
    applyPagePalette();
    paintHighlightOverlay();
  }
  void present() {
    render();
    display_.displayBuffer();
  }
  // smooth: write each pixel at its smooth-text level (as the LCD shows it)
  // instead of plain black and white.
  bool savePgm(const std::string& path,bool smooth=false) {
    render();
    auto* pixels=display_.getFrameBuffer();
    std::vector<uint8_t> levels;
    if(smooth) {
      levels.resize(static_cast<size_t>(375)*SCREEN_H);
      if(!display_.currentAlpha(levels.data()))levels.clear();
    }
    std::FILE* file=std::fopen(path.c_str(),"wb");
    if(!file) return false;
    bool ok=std::fprintf(file,"P5\n375 320\n255\n")>0;
    for (int y=0;ok && y<SCREEN_H;++y) {
      for (int x=0;x<375;++x) {
        bool paper=(pixels[y*47+x/8]&(0x80u>>(x&7)))!=0;
        unsigned char c=paper?255:0;
        if(!levels.empty())c=static_cast<unsigned char>(255-levels[static_cast<size_t>(y)*375+x]);
        if(std::fwrite(&c,1,1,file)!=1){ok=false;break;}
      }
    }
    if(std::fclose(file)!=0)ok=false;
    return ok;
  }
  const std::vector<books::SelectedWord>& selectableWordsForTest() const {return selectedWords_;}
  // How closely the TrueType glyphs land on CrossPoint's own, like for like:
  // the intersection over union of pixels at least half covered (CrossPoint's
  // from its 4-level planes), and the shift of the coverage-weighted ink
  // centre (dx, dy in pixels). A wrong size or offset shows in both.
  // -1 when the page has no smooth glyphs.
  double smoothGlyphAgreement(size_t* pixels=nullptr,double* dx=nullptr,double* dy=nullptr) {
    cpfontAlpha_.clear();
    clearRasterCache();
    render();
    std::vector<uint8_t> now(HalDisplay::PIXELS);
    if(cpfontAlpha_.size()!=HalDisplay::PIXELS || !display_.currentAlpha(now.data()))return -1;
    size_t both=0,either=0;
    double wa=0,xa=0,ya=0,wb=0,xb=0,yb=0;
    for(int y=TOP;y<BOTTOM;++y)
      for(int x=0;x<375;++x) {
        const size_t i=static_cast<size_t>(y)*375+x;
        const unsigned a=cpfontAlpha_[i],b=now[i];
        both+=(a>=128)&&(b>=128);either+=(a>=128)||(b>=128);
        wa+=a;xa+=double(a)*x;ya+=double(a)*y;
        wb+=b;xb+=double(b)*x;yb+=double(b)*y;
      }
    if(pixels)*pixels=either;
    if(dx)*dx=wa>0&&wb>0?xb/wb-xa/wa:0;
    if(dy)*dy=wa>0&&wb>0?yb/wb-ya/wa:0;
    return either?static_cast<double>(both)/static_cast<double>(either):-1;
  }
  // Ink bounding box of the book area of the rendered page (no footer).
  bool inkBounds(int& x0,int& y0,int& x1,int& y1) {
    render();
    const uint8_t* pixels=display_.getFrameBuffer();
    if(!pixels)return false;
    x0=y0=INT32_MAX;x1=y1=-1;
    for(int y=TOP;y<BOTTOM;++y)
      for(int x=0;x<375;++x)
        if(!(pixels[y*47+x/8]&(0x80u>>(x&7)))) {
          x0=std::min(x0,x);x1=std::max(x1,x);y0=std::min(y0,y);y1=std::max(y1,y);
        }
    return x1>=0;
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
    std::printf("Books validated %s: page %zu/%zu, spine %d/%d\n",
        bookPath_.c_str(),currentPage_+1,pageCount(),
        currentSpine_+1,metadata().getSpineCount());
  }
};


// Q2 Pod card-app launcher: the card executable receives no book-path argv.
// The Books shelf is intentionally independent of the stock reader and only
// discovers regular, non-hidden EPUB files from the SD card (one subdir deep).
using books::ShelfEntry;
using books::scanCardBooks;
std::string clippedLabel(const std::string& label,size_t maxCodepoints) {
  return books::clippedUiLabel(label,maxCodepoints);
}

class CardShelf {
  const std::vector<ShelfEntry>& books_;
  HalDisplay display_;
  GfxRenderer renderer_;
  EpdFont font_;
  books::ScriptFontFallback scriptFonts_;
  FontDecompressor decompressor_;
  FontCacheManager fontCache_;
  size_t selected_ = 0;  // index within filtered browsing rows
  size_t current_ = 0;   // index within the unfiltered library
  std::vector<size_t> visible_;
  // Folders mode: the folder shown ("" is Books) and its child folders,
  // listed before its books.
  std::string root_;
  std::string folder_;
  std::vector<std::string> folders_;
  std::string searchQuery_;
  bool searchEditing_=false;
  size_t searchKey_=0;          // the keypad key the wheel is on
  books::T9Keypad keypad_;
  books::ShelfSortMode sortMode_=books::ShelfSortMode::Title;
  std::vector<books::ShelfDetails> cachedDetails_;
  std::vector<bool> detailsChecked_;
  books::ShelfCover currentCover_{};
  bool currentCoverChecked_=false;
  bool currentCoverAvailable_=false;
  bool browsing_ = false;
  bool flowView_=true;
  struct FlowSlot {size_t index=0; bool used=false; books::FlowCover cover;};
  std::array<FlowSlot,8> flowSlots_{};
  books::FlowCover fallbackCover_=books::flow::placeholder();
  std::vector<uint32_t> flowFrame_;
  std::vector<uint8_t> flowAlpha_;
  size_t flowProgressIndex_=size_t(-1);
  int flowProgressPercent_=-1;
  float flowPosition_=0;
  bool flowMoving_=false;
  bool flowTouch_=false;
  int flowTouchX_=0,flowTouchY_=0;
  float flowTouchStart_=0;
  bool flowDragged_=false;
  ReaderClock::time_point flowAt_{};
  static std::string viewFile() {return books::uiStateDir()+"/library-view.conf";}
  static bool loadFlowView() {
    FILE* f=std::fopen(viewFile().c_str(),"rb");
    if(!f)return true;
    char line[32]{};
    const bool list=std::fgets(line,sizeof(line),f) && std::strcmp(line,"list\n")==0;
    std::fclose(f);return !list;
  }
  bool saveFlowView() const {
    const auto path=viewFile(),tmp=path+".tmp";
    FILE* f=std::fopen(tmp.c_str(),"wb");
    if(!f)return false;
    bool ok=std::fputs(flowView_?"cover\n":"list\n",f)>=0;
    if(std::fflush(f)!=0)ok=false;
    if(ok&&fsync(fileno(f))!=0)ok=false;
    if(std::fclose(f)!=0)ok=false;
    if(ok&&std::rename(tmp.c_str(),path.c_str())==0)return true;
    std::remove(tmp.c_str());return false;
  }
  bool onlineMode_=false;
  std::string onlineUrl_;
  std::string onlineTitle_;
  std::vector<books::OpdsEntry> onlineEntries_;
  std::vector<std::string> onlineHistory_;
  size_t onlineSelection_=0;
  books::OpdsFeed onlineFeed_;     // links of the page shown (search, next)
  bool onlineSearching_=false;        // the keyboard is for a catalog search
  // M4: Online opens the list of saved catalogs (connected.conf); a server
  // can be added, edited and deleted there with the T9 keyboard.
  enum class OnlineScreen {Servers, Form, Catalog};
  OnlineScreen onlineScreen_=OnlineScreen::Servers;
  books::ConnectedConfig config_;
  books::OpdsServer server_;          // the catalog being browsed
  size_t serverSelection_=0;
  bool serverEditMode_=false;         // Centre on a server edits it
  books::OpdsServer form_;            // the server being added or edited
  int formIndex_=-1;                  // its place in config_, -1 when new
  size_t formSelection_=0;
  bool formDeleteArmed_=false;
  bool fieldEditing_=false;           // the keyboard edits a form field
  size_t fieldIndex_=0;
  // A catalog book's details: its cover, title, author and description.
  bool detailsOpen_=false;
  books::OpdsEntry detailsEntry_;
  books::OpdsCover detailsCover_{};
  bool detailsHasCover_=false;
  bool detailsOnCard_=false;
  std::vector<std::string> detailsLines_;
  size_t detailsScroll_=0;
  static constexpr int DETAILS_X=124, DETAILS_W=236;
  size_t detailsRoom_=6;  // description lines the last paint had room for
  bool searchSubmit_=false;
  int homeSelection_ = 0;
  std::string notice_;
  // 4 rows of 47 px: a title line and an author line need about 45 px with
  // Vietnamese stacked marks; 5 rows of 38 px made the two lines touch.
  static constexpr int TOP_Y=78, ROW_H=47, VISIBLE_ROWS=4;
  static constexpr int HOME_ONLINE_X=252;

 public:
  explicit CardShelf(const std::vector<ShelfEntry>& books,size_t selection=0,
                     size_t current=0,
                     books::ShelfSortMode sortMode=books::ShelfSortMode::Title,
                     std::string root={}):
      books_(books),root_(std::move(root)),renderer_(display_),font_(&notosans_12_regular),
      fontCache_(renderer_.getFontMap(),renderer_.getSdCardFonts(),renderer_.getTtfFonts()) {
    sortMode_=sortMode;
    flowView_=loadFlowView();
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
    books::applyThemePalette(display_,true);
    books::UiText::instance().setThemeFace(books::uiTextFace());
    renderer_.insertFont(1,EpdFontFamily(12,&font_));
    // All scripts in EPUB filenames use the same SD font fallback machinery.
    scriptFonts_.load(renderer_,12,{1});
    if(!decompressor_.init())return false;
    fontCache_.setFontDecompressor(&decompressor_);
    renderer_.setFontCacheManager(&fontCache_);
    return true;
  }
  size_t selected() const {
    const size_t first=folderRows();
    return browsing_ && selected_>=first && selected_-first<visible_.size()?
        visible_[selected_-first]:current_;
  }
  bool flowActive() const {
    return flowView_ && browsing_ && !onlineMode_ && !searchEditing_ &&
        folderRows()==0 && !visible_.empty();
  }
  bool viewChosen() const {return !browsing_ && homeSelection_==4;}
  void toggleView() {
    flowView_=!flowView_;
    if(!saveFlowView()) {flowView_=!flowView_;notice_="Cannot save setting";}
    flowMoving_=false;flowPosition_=float(selected_);
  }
  bool animating() const {return flowActive()&&flowMoving_&&!flowTouch_;}
  bool flowTouchEvent(const books::PhysicalEvent& event) {
    using K=books::PhysicalKind;
    if(!flowActive()) {flowTouch_=false;return false;}
    if(event.kind==K::TouchDown && event.y>=books::flow::top && event.y<books::flow::bottom) {
      flowTouch_=true;flowDragged_=false;flowTouchX_=event.x;flowTouchY_=event.y;
      flowTouchStart_=flowMoving_?flowPosition_:float(selected_);
      flowMoving_=false;return true;
    }
    if(!flowTouch_)return false;
    if(event.kind!=K::TouchMove && event.kind!=K::TouchUp) {flowTouch_=false;return false;}
    const int dx=event.x-flowTouchX_,dy=event.y-flowTouchY_;
    if(std::abs(dx)>12 && std::abs(dx)>std::abs(dy))flowDragged_=true;
    if(flowDragged_) {
      flowPosition_=std::clamp(flowTouchStart_-float(dx)/90.f,0.f,float(visible_.size()-1));
      selected_=size_t(std::lround(flowPosition_));
      if(event.kind==K::TouchUp) {
        flowTouch_=false;flowMoving_=true;flowAt_=ReaderClock::now();
      }
      present();return true;
    }
    if(event.kind==K::TouchUp) {flowTouch_=false;return false;}
    return true;
  }
  bool folderMode() const {
    return sortMode_==books::ShelfSortMode::Folders && searchQuery_.empty() && !root_.empty();
  }
  size_t folderRows() const {return folderMode()?folders_.size():0;}
  size_t rowCount() const {return folderRows()+visible_.size();}
  bool folderSelected() const {return browsing_ && !searchEditing_ && selected_<folderRows();}
  void openSelectedFolder() {
    if(!folderSelected())return;
    folder_=folder_.empty()?folders_[selected_]:folder_+"/"+folders_[selected_];
    selected_=0;refreshVisible();notice_.clear();
  }
  // Back in a folder: its parent, with the folder just left selected.
  bool folderUp() {
    if(!folderMode() || folder_.empty())return false;
    const auto slash=folder_.rfind('/');
    const std::string child=slash==std::string::npos?folder_:folder_.substr(slash+1);
    folder_=books::shelfParentFolder(folder_);
    refreshVisible();
    const auto it=std::find(folders_.begin(),folders_.end(),child);
    selected_=it==folders_.end()?0:static_cast<size_t>(it-folders_.begin());
    notice_.clear();
    return true;
  }
  bool empty() const {return books_.empty();}
  bool hasMatches() const {return rowCount()>0;}
  bool browsing() const {return browsing_;}
  bool browseChosen() const {return !browsing_ && (homeSelection_==1 || books_.empty());}
  bool sortChosen() const {return !browsing_ && homeSelection_==2;}
  bool onlineChosen() const {return !browsing_ && homeSelection_==3;}
  bool onlineMode() const {return onlineMode_;}
  bool searching() const {return browsing_ && searchEditing_;}
  // The server list: no network until a server is opened.
  bool enterOnline() {
    config_=books::loadConnectedConfig();
    onlineMode_=true;browsing_=true;searchEditing_=false;fieldEditing_=false;
    onlineScreen_=OnlineScreen::Servers;serverEditMode_=false;
    onlineEntries_.clear();onlineHistory_.clear();
    serverSelection_=0;
    notice_=config_.servers.empty()?"Add a catalog server":"";
    return true;
  }
  bool detailsOpen() const {return onlineMode_ && detailsOpen_;}
  const books::OpdsEntry& detailsEntry() const {return detailsEntry_;}
  void openDetails(const books::OpdsEntry& entry,const std::string& bookDir) {
    detailsEntry_=entry;
    detailsHasCover_=books::fetchOpdsCover(entry,server_,detailsCover_);
    struct stat info{};
    const std::string file=books::opdsDestination(bookDir,entry);
    detailsOnCard_=!file.empty() && ::lstat(file.c_str(),&info)==0 && S_ISREG(info.st_mode);
    detailsLines_=wrapSmall(entry.summary.empty()?books::tr("No description"):entry.summary,DETAILS_W);
    if(detailsLines_.size()>200)detailsLines_.resize(200);
    detailsScroll_=0;detailsOpen_=true;notice_.clear();
  }
  void closeDetails() {detailsOpen_=false;notice_.clear();}
  int smallWidth(const std::string& text) const {
    int width=0;
    const auto* p=reinterpret_cast<const unsigned char*>(text.c_str());
    while(*p) {
      const uint32_t cp=utf8NextCodepoint(&p);
      int32_t advance=0;int top=0;
      if(renderer_.getCodepointMetrics(1,cp,EpdFontFamily::REGULAR,advance,top))
        width+=fp4::toPixel(advance*150/256);
    }
    return width;
  }
  // Words to lines of at most |width| pixels in the small (150/256) face; a
  // word wider than a line is cut at a whole character.
  std::vector<std::string> wrapSmall(const std::string& text,int width) const {
    std::vector<std::string> lines;
    std::string line;
    size_t i=0;
    while(i<text.size()) {
      size_t end=text.find(' ',i);
      if(end==std::string::npos)end=text.size();
      std::string word=text.substr(i,end-i);
      i=end+1;
      if(word.empty())continue;
      const std::string joined=line.empty()?word:line+" "+word;
      if(smallWidth(joined)<=width){line=joined;continue;}
      if(!line.empty())lines.push_back(line);
      line.clear();
      while(smallWidth(word)>width) {  // a long URL or an unspaced script
        size_t cut=word.size();
        while(cut>1 && smallWidth(word.substr(0,cut))>width) {
          --cut;
          while(cut>0 && (static_cast<unsigned char>(word[cut])&0xC0)==0x80)--cut;
        }
        if(cut==0)cut=1;
        lines.push_back(word.substr(0,cut));
        word.erase(0,cut);
      }
      line=word;
    }
    if(!line.empty())lines.push_back(line);
    return lines;
  }
  bool serverScreen() const {
    return onlineMode_ && !searchEditing_ && onlineScreen_!=OnlineScreen::Catalog;
  }
  bool fieldEditing() const {return onlineMode_ && fieldEditing_ && searchEditing_;}
  const books::OpdsServer& server() const {return server_;}
  // Rows of the server list: the servers, then Add (and Edit, when any).
  size_t serverRows() const {
    if(serverEditMode_)return config_.servers.size()+1;  // + Cancel
    return config_.servers.size()+1+(config_.servers.empty()?0:1);
  }
  size_t formRows() const {return formIndex_<0?5:6;}
  enum class ServerAction {None, Redraw, Open};
  // Wheel, Centre, Back and touch on the server list and the server form.
  ServerAction serverInput(books::LogicalAction action,int touchX,int touchY,bool touch) {
    using A=books::LogicalAction;
    const bool form=onlineScreen_==OnlineScreen::Form;
    size_t& pick=form?formSelection_:serverSelection_;
    const size_t rows=form?formRows():serverRows();
    if(action==A::Back || (touch && touchY<73)) {
      notice_.clear();
      if(form)onlineScreen_=OnlineScreen::Servers;
      else if(serverEditMode_){serverEditMode_=false;serverSelection_=0;}
      else goHome();
      return ServerAction::Redraw;
    }
    if(action==A::Next || action==A::Previous) {
      const size_t before=pick;
      if(action==A::Next && pick+1<rows)++pick;
      if(action==A::Previous && pick>0)--pick;
      if(pick!=before){notice_.clear();if(form)formDeleteArmed_=false;}
      return pick!=before?ServerAction::Redraw:ServerAction::None;
    }
    if(touch) {
      if(touchY<TOP_Y)return ServerAction::None;
      const size_t start=(pick/VISIBLE_ROWS)*VISIBLE_ROWS;
      const size_t row=static_cast<size_t>((touchY-TOP_Y)/ROW_H);
      if(row>=VISIBLE_ROWS || start+row>=rows)return ServerAction::None;
      if(form && pick!=start+row)formDeleteArmed_=false;
      pick=start+row;
    } else if(action!=A::Confirm)return ServerAction::None;
    notice_.clear();
    if(form)return formChoice();
    const size_t count=config_.servers.size();
    if(serverEditMode_) {
      if(pick>=count){serverEditMode_=false;serverSelection_=0;return ServerAction::Redraw;}
      openForm(static_cast<int>(pick));return ServerAction::Redraw;
    }
    if(pick<count){server_=config_.servers[pick];return ServerAction::Open;}
    if(pick==count) {
      if(count>=books::kMaxOpdsServers){notice_="Up to 8 servers";return ServerAction::Redraw;}
      openForm(-1);return ServerAction::Redraw;
    }
    serverEditMode_=true;serverSelection_=0;
    return ServerAction::Redraw;
  }
  void openForm(int index) {
    formIndex_=index;
    form_=index<0?books::OpdsServer{}:config_.servers[static_cast<size_t>(index)];
    formSelection_=index<0?1:0;  // a new server starts at its URL
    formDeleteArmed_=false;
    onlineScreen_=OnlineScreen::Form;
  }
  std::string* formField(size_t row) {
    switch(row) {
      case 0: return &form_.name;
      case 1: return &form_.url;
      case 2: return &form_.username;
      case 3: return &form_.password;
      default: return nullptr;
    }
  }
  ServerAction formChoice() {
    if(std::string* field=formField(formSelection_)) {
      fieldIndex_=formSelection_;
      searchQuery_=*field;
      fieldEditing_=true;searchEditing_=true;onlineSearching_=false;
      keypad_.reset();
      keypad_.setUrlMode(formSelection_!=0);
      searchKey_=books::T9Keypad::Abc;
      return ServerAction::Redraw;
    }
    auto saved=books::loadConnectedConfig();  // keep the sync settings as they are now
    saved.servers=config_.servers;
    if(formSelection_==4) {  // Save
      form_.url=books::trimConnected(form_.url);
      if(auto problem=books::opdsServerProblem(form_);!problem.empty()) {
        notice_=problem;return ServerAction::Redraw;
      }
      if(formIndex_<0)saved.servers.push_back(form_);
      else saved.servers[static_cast<size_t>(formIndex_)]=form_;
    } else {  // Delete, on a second press
      if(!formDeleteArmed_){formDeleteArmed_=true;return ServerAction::Redraw;}
      saved.servers.erase(saved.servers.begin()+formIndex_);
    }
    std::string error;
    if(!books::saveConnectedConfig(saved,error)){notice_=error;return ServerAction::Redraw;}
    const bool removed=formSelection_==5;
    config_=books::loadConnectedConfig();
    onlineScreen_=OnlineScreen::Servers;serverEditMode_=false;
    serverSelection_=removed||config_.servers.empty()?0:
        formIndex_<0?config_.servers.size()-1:static_cast<size_t>(formIndex_);
    notice_=removed?"Server deleted":"Server saved";
    return ServerAction::Redraw;
  }
  // "Offline: 22 Nov 13:05": the page is the copy kept on the card.
  static std::string offlineNotice(long long savedAt) {
    char when[32]="";
    const std::time_t t=static_cast<std::time_t>(savedAt);
    std::tm local{};
    if(savedAt>0 && localtime_r(&t,&local))std::strftime(when,sizeof(when)," %d/%m %H:%M",&local);
    return std::string(books::tr("Offline copy"))+when;
  }
  // Opens server_'s catalog; on failure the server list stays, with why.
  bool openServer() {
    onlineHistory_.clear();
    return loadOnlinePage(server_.url,false);
  }
  bool loadOnlinePage(const std::string& url,bool saveHistory) {
    auto feed=books::fetchOpdsFeed(url,server_);
    if(!feed.error.empty()) {
      notice_=feed.error;
      return false;
    }
    if(saveHistory && onlineHistory_.size()<12)
      onlineHistory_.push_back(onlineUrl_);
    onlineUrl_=url;
    const bool offline=feed.offline;
    const long long savedAt=feed.savedAt;
    showFeed(std::move(feed));
    onlineScreen_=OnlineScreen::Catalog;
    onlineMode_=true;
    browsing_=true;
    searchEditing_=false;
    notice_.clear();
    if(offline)notice_=offlineNotice(savedAt);
    return true;
  }
  void returnFromOnline() {
    if(onlineHistory_.empty()) {  // the catalog's first page: back to the servers
      onlineEntries_.clear();onlineScreen_=OnlineScreen::Servers;notice_.clear();return;
    }
    const auto previous=onlineHistory_.back();
    onlineHistory_.pop_back();
    // A failed network fetch must not strand the user: back to the servers.
    auto feed=books::fetchOpdsFeed(previous,server_);
    if(!feed.error.empty()) {
      onlineEntries_.clear();onlineHistory_.clear();
      onlineScreen_=OnlineScreen::Servers;notice_=feed.error;return;
    }
    onlineUrl_=previous;
    const bool offline=feed.offline;
    const long long savedAt=feed.savedAt;
    showFeed(std::move(feed));
    notice_=offline?offlineNotice(savedAt):std::string();
  }
  // M4: a feed's search link becomes a first "Search" row and its rel="next"
  // link a last "More results" row (OPDS catalogs page long lists).
  void showFeed(books::OpdsFeed feed) {
    onlineTitle_=feed.title.empty()?"Connected library":feed.title;
    onlineEntries_=std::move(feed.entries);
    feed.entries.clear();
    if(!feed.searchTemplate.empty() || !feed.searchDescription.empty()) {
      books::OpdsEntry search;
      search.kind=books::OpdsEntry::Kind::Search;
      search.title="Search this catalog";
      onlineEntries_.insert(onlineEntries_.begin(),std::move(search));
    }
    if(!feed.nextHref.empty()) {
      books::OpdsEntry more;
      more.kind=books::OpdsEntry::Kind::More;
      more.title="More results";
      more.href=feed.nextHref;
      onlineEntries_.push_back(std::move(more));
    }
    onlineFeed_=std::move(feed);
    onlineSelection_=0;
    onlineSearching_=false;searchEditing_=false;
  }
  void enterOnlineSearch() {
    onlineSearching_=true;searchEditing_=true;searchKey_=books::T9Keypad::Abc;searchQuery_.clear();
    keypad_.reset();
    notice_.clear();
  }
  bool onlineSearching() const {return onlineMode_ && onlineSearching_ && searchEditing_;}
  // DONE on the catalog keyboard asks for the search (applySearchChoice).
  bool takeSearchSubmit() {const bool r=searchSubmit_;searchSubmit_=false;return r;}
  // Run the typed query against the catalog; the results page is pushed on
  // the history, so Back returns to the page that was searched.
  bool submitOnlineSearch() {
    std::string error;
    const std::string url=books::opdsSearchUrl(onlineFeed_,searchQuery_,error,server_);
    if(url.empty()) {
      notice_=searchQuery_.empty()?"Type a search first":error;
      return false;
    }
    if(!loadOnlinePage(url,true))return false;  // showFeed closes the keyboard
    if(onlineEntries_.empty())notice_="No results";
    return true;
  }
  bool onlineHasSelection() const {return onlineSelection_<onlineEntries_.size();}
  books::OpdsEntry onlineEntry() const {
    return onlineHasSelection()?onlineEntries_[onlineSelection_]:
      books::OpdsEntry{};
  }
  void onlineTouch(int x,int y) {
    if(y<TOP_Y || y>=TOP_Y+VISIBLE_ROWS*ROW_H)return;
    const size_t start=(onlineSelection_/VISIBLE_ROWS)*VISIBLE_ROWS;
    const size_t selected=start+static_cast<size_t>((y-TOP_Y)/ROW_H);
    if(selected<onlineEntries_.size())onlineSelection_=selected;
  }
  void enterBrowser() {
    browsing_=true;searchEditing_=false;notice_.clear();
    if(folderMode() && current_<books_.size()) {  // open the current book's folder, on it
      std::string where;
      folder_=books::shelfRelativeFolder(root_,books_[current_].path,where)?where:std::string();
      refreshVisible();
      const auto it=std::find(visible_.begin(),visible_.end(),current_);
      selected_=it==visible_.end()?0:folderRows()+static_cast<size_t>(it-visible_.begin());
    }
  }
  void goHome() {
    browsing_=false;onlineMode_=false;searchEditing_=false;homeSelection_=0;
    fieldEditing_=false;serverEditMode_=false;onlineScreen_=OnlineScreen::Servers;
    detailsOpen_=false;
    keypad_.setUrlMode(false);
    searchQuery_.clear();refreshVisible();notice_.clear();
  }
  void enterSearch() {
    browsing_=true;searchEditing_=true;searchKey_=books::T9Keypad::Abc;keypad_.reset();
    notice_.clear();
  }
  void endSearch() {
    searchEditing_=false;onlineSearching_=false;fieldEditing_=false;
    keypad_.setUrlMode(false);notice_.clear();
  }
  static constexpr size_t SEARCH_KEY_COUNT=books::kT9Keys;
  // The keypad: rows of 4 keys under the field (Q2's own T9 layout).
  static constexpr int KB_X=12, KB_Y=112, KB_W=86, KB_H=37, KB_GAP=2;
  bool applySearchChoice() {
    const auto result=keypad_.press(static_cast<int>(searchKey_),searchQuery_,
                                    fieldEditing_?(fieldIndex_==1?400:fieldIndex_==0?64:128):
                                    onlineSearching_?40:24);
    if(result==books::T9Result::Done && fieldEditing_) {
      if(std::string* field=formField(fieldIndex_))*field=searchQuery_;
      searchQuery_.clear();endSearch();formDeleteArmed_=false;
      return true;
    }
    if(fieldEditing_)return true;
    if(result==books::T9Result::Done) {
      if(onlineSearching_){searchSubmit_=true;return true;}
      endSearch();return true;
    }
    if(onlineSearching_)return true;  // a catalog query: nothing local to filter
    refreshVisible();
    selected_=0;
    return true;
  }
  void refreshVisible() {
    visible_.clear();
    folders_.clear();
    if(folderMode()) {
      auto view=books::shelfFolderView(books_,root_,folder_);
      if(view.folders.empty() && view.books.empty() && !folder_.empty()) {
        folder_.clear();view=books::shelfFolderView(books_,root_,folder_);
      }
      folders_=std::move(view.folders);visible_=std::move(view.books);
      if(selected_>=rowCount())selected_=0;
      return;
    }
    for(size_t i=0;i<books_.size();++i) {
      if(searchQuery_.empty()) {visible_.push_back(i);continue;}
      const auto& info=details(i);
      if(books::shelfSearchMatches(
          searchQuery_,books_[i].label,info.title,info.author))
        visible_.push_back(i);
    }
    if(visible_.empty() || selected_>=visible_.size())selected_=0;
  }
  const std::string& bookPath() const {return books_[selected()].path;}
  bool change(int direction) {
    notice_.clear();
    if(onlineMode_ && !searchEditing_ && detailsOpen_) {
      const size_t last=detailsLines_.size()>detailsRoom_?detailsLines_.size()-detailsRoom_:0;
      const size_t before=detailsScroll_;
      if(direction>0 && detailsScroll_<last)++detailsScroll_;
      else if(direction<0 && detailsScroll_>0)--detailsScroll_;
      return before!=detailsScroll_;
    }
    if(onlineMode_ && !searchEditing_) {
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
      const int next=std::clamp(homeSelection_+direction,0,4);
      if(next==homeSelection_)return false;
      homeSelection_=next;
      return true;
    }
    if(rowCount()==0)return false;
    const auto before=selected_;
    if(direction>0 && selected_+1<rowCount())++selected_;
    else if(direction<0 && selected_>0)--selected_;
    if(selected_!=before && flowActive()) {
      if(!flowMoving_)flowPosition_=float(before);
      flowMoving_=true;flowAt_=ReaderClock::now();
    }
    return selected_!=before;
  }
  bool tap(int x,int y) {
    if(x<0||x>=375)return false;
    if(onlineMode_ && !searchEditing_) {
      if(y<73) {returnFromOnline();return true;}
      onlineTouch(x,y);return true;
    }
    if(searchEditing_) {
      if(y>=269){endSearch();return true;}
      if(y<KB_Y||x<KB_X)return false;
      const size_t column=static_cast<size_t>((x-KB_X)/(KB_W+KB_GAP));
      const size_t row=static_cast<size_t>((y-KB_Y)/(KB_H+KB_GAP));
      if(column>=4||row>=4)return false;
      const size_t option=row*4+column;
      searchKey_=option;
      return applySearchChoice();
    }
    if(!browsing_) {
      if(y>=158&&y<195)homeSelection_=0;
      else if(y>=204&&y<241)homeSelection_=1;
      else if(y>=244&&y<267)homeSelection_=x<HOME_ONLINE_X?2:3;
      else if(y>=269&&y<301)homeSelection_=4;
      else return false;
      return true;
    }
    if(y>=39 && y<74) {
      if(x>=190)return enterOnline();
      enterSearch();return true;
    }
    if(flowActive()) {
      if(y<books::flow::top || y>=books::flow::bottom)return false;
      const float position=flowMoving_?flowPosition_:float(selected_);
      size_t picked=visible_.size();
      float nearest=books::flow::reach;
      for(size_t row=size_t(std::max(0,int(std::floor(position-books::flow::reach))));
          row<visible_.size() && float(row)-position<books::flow::reach;++row) {
        const float d=float(row)-position;
        if(std::abs(d)>=nearest)continue;
        if(books::flow::hit(flowCoverAt(row),d,x,y)) {picked=row;nearest=std::abs(d);}
      }
      if(picked==visible_.size())return false;
      if(picked==selected_ && !flowMoving_)return true;
      flowPosition_=position;selected_=picked;
      flowMoving_=true;flowAt_=ReaderClock::now();
      return false;
    }
    if(rowCount()==0||y<TOP_Y||y>=TOP_Y+VISIBLE_ROWS*ROW_H)return false;
    const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
    const size_t index=start+static_cast<size_t>((y-TOP_Y)/ROW_H);
    if(index>=rowCount())return false;
    selected_=index;
    return true;
  }
  const books::ShelfDetails& details(size_t index) {
    if(!detailsChecked_[index]) {
      detailsChecked_[index]=true;
      (void)books::loadShelfDetails(books_[index].path,cachedDetails_[index]);
    }
    return cachedDetails_[index];
  }
  void drawBookCover(int x,int y,size_t index) {
    if(!currentCoverChecked_) {
      currentCoverAvailable_=books::loadShelfCover(books_[index].path,currentCover_);
      currentCoverChecked_=true;
    }
    renderer_.drawLine(x-1,y-1,x+books::ShelfCover::width,y-1,true);
    renderer_.drawLine(x-1,y-1,x-1,y+books::ShelfCover::height,true);
    renderer_.drawLine(x+books::ShelfCover::width,y-1,
                       x+books::ShelfCover::width,y+books::ShelfCover::height,true);
    renderer_.drawLine(x-1,y+books::ShelfCover::height,
                       x+books::ShelfCover::width,y+books::ShelfCover::height,true);
    if(!currentCoverAvailable_) {
      renderer_.drawLine(x+7,y+12,x+39,y+12,true);
      renderer_.drawLine(x+7,y+18,x+39,y+18,true);
      return;
    }
    for(int py=0;py<books::ShelfCover::height;++py)
      for(int px=0;px<books::ShelfCover::width;++px)
        if(currentCover_.ink[py*books::ShelfCover::stride+px/8] &
           (0x80u>>(px&7)))
          renderer_.drawPixel(x+px,y+py,true);
    books::paintPicture(display_,x,y,books::ShelfCover::width,books::ShelfCover::height);
  }
  void onIndexedBook(const std::string& path) {
    // A new cache file may have appeared since this shelf object was made.
    // Refresh only that book's lazy metadata (and current cover, if any).
    for(size_t i=0;i<books_.size();++i) {
      if(books_[i].path!=path)continue;
      for(auto& slot:flowSlots_)if(slot.used&&slot.index==i)slot.used=false;
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
    // UI labels, translated; UTF-8 so translations keep their accents.
    const std::string text=books::tr(label);
    {
      auto& ui=books::UiText::instance();
      const bool classic=books::uiTheme().style==books::UiTheme::Style::Classic;
      const int px=std::max(classic?16:13,scale*24/256);
      if(ui.available() && ui.drawable(text) &&
         uiDraw(x,baseline-ui.ascent(px),text,px,362))return;
    }
    const auto* p=reinterpret_cast<const unsigned char*>(text.c_str());
    while(*p) {
      const uint32_t cp=utf8NextCodepoint(&p);
      x+=renderer_.drawScaledCodepoint(1,cp,EpdFontFamily::REGULAR,x,baseline,scale);
    }
  }
  // A list row's second line (an author), in font 1's own glyphs scaled down:
  // two full-size lines of font 1 (34 px apart) cannot share a 38 px row, and
  // drawing the second at +18 px overprinted the next row. Draws nothing and
  // returns false when font 1 lacks a character (CJK comes from a fallback
  // font that scaled drawing does not reach), rather than dropping letters.
  bool drawSmallLine(int x,int baseline,const std::string& text,int maxWidth,int scale=150) {
    int totalWidth=0;
    const auto* probe=reinterpret_cast<const unsigned char*>(text.c_str());
    while(*probe) {
      const uint32_t cp=utf8NextCodepoint(&probe);
      int32_t advance=0;int top=0;
      if(!renderer_.getCodepointMetrics(1,cp,EpdFontFamily::REGULAR,advance,top))return false;
      totalWidth+=fp4::toPixel(advance*scale/256);
    }
    int limit=x+maxWidth;
    if(totalWidth>maxWidth) {
      int32_t advance=0;int top=0;
      if(renderer_.getCodepointMetrics(1,'.',EpdFontFamily::REGULAR,advance,top))
        limit-=3*fp4::toPixel(advance*scale/256);
    }
    const auto* p=reinterpret_cast<const unsigned char*>(text.c_str());
    int cx=x;
    while(*p) {
      const uint32_t cp=utf8NextCodepoint(&p);
      int32_t advance=0;int top=0;
      (void)renderer_.getCodepointMetrics(1,cp,EpdFontFamily::REGULAR,advance,top);
      const int width=fp4::toPixel(advance*scale/256);
      if(cx+width>limit) {  // clip at a whole character, with an ellipsis
        for(int dot=0;dot<3;++dot)
          cx+=renderer_.drawScaledCodepoint(1,'.',EpdFontFamily::REGULAR,cx,baseline,scale);
        break;
      }
      cx+=renderer_.drawScaledCodepoint(1,cp,EpdFontFamily::REGULAR,cx,baseline,scale);
    }
    return true;
  }
  void invertRect(int x0,int y0,int width,int height) {books::paintSelection(display_,x0,y0,width,height);}
  // One book or catalog row: the title in font 1, an author beneath it in a
  // smaller line when there is one (else the title is centred in the row).
  // Clip at a whole character to a pixel width: wide CJK titles clipped by a
  // character count still ran past the row's right edge.
  std::string fitWidth(std::string text,int maxWidth,int uiPx=0) const {
    const auto width=[this,uiPx](const std::string& value) {
      return uiPx?books::UiText::instance().width(value,uiPx):renderer_.getTextWidth(1,value.c_str());
    };
    if(width(text)<=maxWidth)return text;
    const std::string dots="...";
    while(!text.empty()) {
      size_t cut=text.size()-1;
      while(cut>0 && (static_cast<unsigned char>(text[cut])&0xC0)==0x80)--cut;
      text.erase(cut);
      while(!text.empty() && text.back()==' ')text.pop_back();
      if(width(text+dots)<=maxWidth)return text+dots;
    }
    return dots;
  }
  // Library text in Q2 Pod's TrueType faces (UiText) when they have every
  // character; else font 1 with its script fallbacks, as before.
  static int shelfRowPx() {return books::uiTheme().style==books::UiTheme::Style::Classic?25:22;}
  static int shelfSmallPx() {return books::uiTheme().style==books::UiTheme::Style::Classic?16:14;}
  bool uiDraw(int x,int top,const std::string& text,int px,int maxX) {
    auto& ui=books::UiText::instance();
    if(!ui.available() || !ui.drawable(text))return false;
    ui.draw(display_,x,top,text,px,maxX);
    return true;
  }
  void drawListRow(int y,const std::string& rawTitle,const std::string& author,bool active) {
    constexpr int kHeight=ROW_H-2;
    if(!active)renderer_.drawLine(15,y+kHeight,359,y+kHeight,true);
    auto& ui=books::UiText::instance();
    if(ui.available() && ui.drawable(rawTitle) && (author.empty() || ui.drawable(author))) {
      const int rowPx=author.empty()?shelfRowPx():20;
      const int titleH=ui.lineHeight(rowPx),smallH=ui.lineHeight(shelfSmallPx());
      const int block=author.empty()?titleH:titleH+smallH;
      const int top=y+(kHeight-block)/2;
      ui.draw(display_,22,top,rawTitle,rowPx,356);
      if(!author.empty())ui.draw(display_,24,top+titleH,author,shelfSmallPx(),354);
      if(active)invertRect(12,y,350,kHeight);
      return;
    }
    const std::string title=fitWidth(rawTitle,334);
    const bool second=!author.empty();
    if(second)renderer_.drawText(1,22,y-3,title.c_str(),true);
    else drawRowLabel(22,y,kHeight,title.c_str());
    if(second && !drawSmallLine(24,y+kHeight-4,author,330)) {
      // no glyphs for the author: centre the title alone instead
      renderer_.fillRect(13,y,349,kHeight,false);
      drawRowLabel(22,y,kHeight,title.c_str());
    }
    if(active)invertRect(12,y,350,kHeight);
  }
  void drawRowLabel(int x,int y,int height,const char* label,bool black=true) {
    if(black) {
      auto& ui=books::UiText::instance();
      const std::string text=books::tr(label);
      if(ui.available() && ui.drawable(text) &&
         uiDraw(x,y+(height-ui.lineHeight(shelfRowPx()))/2,text,shelfRowPx(),362))return;
    }
    const int textY=y+(height-renderer_.getLineHeight(1))/2;
    renderer_.drawText(1,x,textY,books::tr(label),black);
  }
  void paint() {
    renderer_.clearScreen(0xff);
    // Fixed title and footer are outside both the home and file-list regions.
    // Invert the header raster so its text stays white on the dark title bar.
    if(books::uiTheme().active()) {
      // Q2 Pod's bar: 26 px, the title centred in the bar's face size.
      const char* title=onlineMode_?"Online":"Books";
      int width=0;
      auto& ui=books::UiText::instance();
      if(ui.available() && ui.drawable(books::tr(title))) {  // Q2 Pod's 21 px bar title
        const std::string name=books::tr(title);
        uiDraw((375-ui.width(name,19))/2,(26-ui.lineHeight(19))/2,name,19,362);
        books::paintBar(display_,0,0,375,26);
        width=-1;
      }
      else for(const char* c=books::tr(title);*c;++c) {
        int32_t advance=0;int top=0;
        if(renderer_.getCodepointMetrics(1,static_cast<unsigned char>(*c),EpdFontFamily::REGULAR,advance,top))
          width+=fp4::toPixel(advance*170/256);
      }
      if(width>=0) {
        drawCompactLabel((375-width)/2,19,title,170);
        books::paintBar(display_,0,0,375,26);
      }
    } else {
      // Centred, as the themed bar: the strip's ends are under the glass.
      const char* title=books::tr(onlineMode_?"Online":"Books");
      drawRowLabel((375-renderer_.getTextWidth(1,title))/2,0,39,title);
      books::paintBar(display_,0,0,375,39);  // the title strip
    }
    if(serverScreen()) {
      paintServers();
    } else if(onlineMode_ && !searchEditing_ && detailsOpen_) {
      paintDetails();
    } else if(onlineMode_ && !searchEditing_) {
      // Placed as Home's header is: drawn at y 49 its glyphs crossed the rule.
      drawRowLabel(16,39,35,
          clippedLabel(onlineTitle_.empty()?"Online library":onlineTitle_,35).c_str());
      renderer_.drawLine(15,74,359,74,true);
      if(onlineEntries_.empty()) {
        renderer_.drawText(1,20,120,books::tr("No EPUBs or folders"));
      }
      const size_t start=(onlineSelection_/VISIBLE_ROWS)*VISIBLE_ROWS;
      for(size_t row=0;row<VISIBLE_ROWS && start+row<onlineEntries_.size();++row) {
        const size_t index=start+row;
        const int y=TOP_Y+static_cast<int>(row)*ROW_H;
        const bool active=index==onlineSelection_;
        const auto& entry=onlineEntries_[index];
        const std::string prefix=
          entry.kind==books::OpdsEntry::Kind::Epub?"[EPUB] ":
          entry.kind==books::OpdsEntry::Kind::Search?"[FIND] ":
          entry.kind==books::OpdsEntry::Kind::More?">> ":"[DIR] ";
        drawListRow(y,clippedLabel(prefix+entry.title,31),entry.author,active);
      }
    } else if(!browsing_) {
      drawRowLabel(16,39,35,"Home");
      constexpr int top=74,bottom=149,right=359;
      constexpr int coverX=18,textX=82,textWidth=right-textX,lineHeight=21;
      constexpr int coverY=(top+bottom-books::ShelfCover::height+1)/2;
      renderer_.drawLine(15,top,right,top,true);
      // The shelf only reads the cached thumbnail; the reader decodes the cover.
      if(!books_.empty())drawBookCover(coverX,coverY,current_);
      else renderer_.drawRect(coverX-1,coverY-1,
                              books::ShelfCover::width+2,books::ShelfCover::height+2,true);

      const auto* info=books_.empty()?nullptr:&details(current_);
      const bool hasAuthor=info && !info->author.empty();
      const int textY=(top+bottom+1-lineHeight*(hasAuthor?3:2))/2;
      drawCompactLabel(textX,textY+16,"Current book",150);
      const int titleY=textY+lineHeight;
      if(info) {
        const auto& title=info->title.empty()?books_[current_].label:info->title;
        auto& ui=books::UiText::instance();
        const int titlePx=shelfRowPx()-3;
        if(ui.available() && ui.drawable(title)) {
          ui.draw(display_,textX,titleY+(lineHeight-ui.lineHeight(titlePx))/2,
                  fitWidth(title,textWidth,titlePx),titlePx,right);
        } else if(!drawSmallLine(textX,titleY+19,title,textWidth,224)) {
          drawRowLabel(textX,titleY,lineHeight,renderer_.truncatedText(1,title.c_str(),textWidth).c_str());
        }
        if(hasAuthor)
          drawRowLabel(textX,titleY+lineHeight,lineHeight,
                       renderer_.truncatedText(1,info->author.c_str(),textWidth).c_str());
      } else {
        drawRowLabel(textX,titleY,lineHeight,"No books yet");
      }
      renderer_.drawLine(15,bottom,right,bottom,true);
      if(!books_.empty()) {
        drawRowLabel(22,158,37,"Continue reading");
        if(homeSelection_==0)books::paintSelection(display_,12,158,350,37);
      }
      drawRowLabel(22,204,37,"Browse files");
      if(homeSelection_==1 || books_.empty())books::paintSelection(display_,12,204,350,37);
      std::string sortText=books::tr("Sort: ");
      sortText+=books::tr(books::shelfSortLabel(sortMode_));
      drawRowLabel(22,244,23,
          renderer_.truncatedText(1,sortText.c_str(),HOME_ONLINE_X-42).c_str());
      drawRowLabel(HOME_ONLINE_X+10,244,23,"Online");
      if(homeSelection_==2)books::paintSelection(display_,12,244,HOME_ONLINE_X-22,23);
      if(homeSelection_==3)books::paintSelection(display_,HOME_ONLINE_X,244,362-HOME_ONLINE_X,23);
      const std::string view=std::string(books::tr("Library View"))+": "+books::tr(flowView_?"Cover Flow":"List View");
      drawRowLabel(68,269,30,view.c_str());
      if(homeSelection_==4)books::paintSelection(display_,62,269,251,30);
    } else if(searchEditing_) {
      // The Q2's T9 keyboard: field, the open key's letters, 4x4 keypad.
      static const char* const fieldNames[4]={"Server name","Catalog URL","User name","Password"};
      drawCompactLabel(16,56,fieldEditing_?fieldNames[std::min<size_t>(fieldIndex_,3)]:
                       onlineSearching_?"Search catalog":"Search library",176);
      char count[48];
      if(fieldEditing_)std::snprintf(count,sizeof(count),"%s",books::tr("Done: keep"));
      else if(onlineSearching_)std::snprintf(count,sizeof(count),"Done: search");
      else std::snprintf(count,sizeof(count),books::tr("%zu / %zu books"),visible_.size(),books_.size());
      drawCompactLabel(232,56,count,136);
      renderer_.drawRect(12,62,351,28,true);
      const int pending=keypad_.pendingKey();
      std::string shown=searchQuery_;
      if(fieldEditing_ && fieldIndex_==3)  // a password: only the open letter shows
        for(size_t i=0;i<shown.size();++i)
          if(i+1<shown.size() || pending<0)shown[i]='*';
      while(shown.size()>30)shown.erase(0,1);
      drawCompactLabel(20,84,shown.c_str(),176);
      // caret, after the text (under the letter still open to the next press)
      int caretX=20;
      for(unsigned char ch:shown) {
        int32_t advance=0;int top=0;
        if(renderer_.getCodepointMetrics(1,ch,EpdFontFamily::REGULAR,advance,top))
          caretX+=fp4::toPixel(advance*176/256);
      }
      if(pending>=0 && !shown.empty()) {
        int32_t advance=0;int top=0;
        (void)renderer_.getCodepointMetrics(1,static_cast<unsigned char>(shown.back()),
                                           EpdFontFamily::REGULAR,advance,top);
        const int w=fp4::toPixel(advance*176/256);
        renderer_.fillRect(caretX-w,86,w,2,true);
      } else renderer_.fillRect(caretX+1,66,2,20,true);
      if(pending>=0) {  // the open key's letters, the typed one inverted
        const std::string set=keypad_.letters(pending);
        for(size_t i=0;i<set.size();++i) {
          const int x=20+static_cast<int>(i)*24;
          const char one[2]={set[i],0};
          drawCompactLabel(x+6,106,one,150);
          if(static_cast<int>(i)==keypad_.pendingIndex())invertRect(x,92,22,18);
        }
      }
      for(int key=0;key<static_cast<int>(SEARCH_KEY_COUNT);++key) {
        const int x=KB_X+(key%4)*(KB_W+KB_GAP), y=KB_Y+(key/4)*(KB_H+KB_GAP);
        const bool fn=books::T9Keypad::functionKey(key);
        const bool focus=static_cast<size_t>(key)==searchKey_;
        const std::string label=keypad_.label(key);
        // Function keys are dark, as Q2's; the wheel's key is inverted.
        if(!fn) {
          renderer_.drawRect(x,y,KB_W,KB_H,true);
          renderer_.drawLine(x+1,y+KB_H,x+KB_W-1,y+KB_H,true);  // the key's foot
        }
        int width=0;
        const int scale=label.size()>3?150:176;
        for(unsigned char ch:label) {
          int32_t advance=0;int top=0;
          if(renderer_.getCodepointMetrics(1,ch,EpdFontFamily::REGULAR,advance,top))
            width+=fp4::toPixel(advance*scale/256);
        }
        const int tx=x+(KB_W-width)/2;
        drawCompactLabel(tx,y+25,label.c_str(),scale);
        // Function keys dark (Done as the selection when themed), as the
        // Q2's own keyboard; the wheel's key is the selection.
        if(fn)books::paintKey(display_,x,y,KB_W,KB_H,key==books::T9Keypad::Done);
        if(focus) {
          invertRect(x,y,KB_W,KB_H);
          renderer_.drawRect(x-1,y-1,KB_W+2,KB_H+2,true);
        }
      }
    } else {
      char count[64];
      std::snprintf(count,sizeof(count),books::tr("Books: %zu/%zu"),visible_.size(),books_.size());
      if(folderMode()) {  // where you are instead of the count
        std::string crumb=books::shelfBreadcrumb(folder_,24);
        if(crumb.compare(0,5,"Books")==0)crumb=books::tr("Books")+crumb.substr(5);
        drawCompactLabel(16,61,crumb.c_str(),160);
      } else drawCompactLabel(16,61,count);
      drawCompactLabel(212,61,"Search | Online",133);
      renderer_.drawLine(15,72,359,72,true);
      if(flowActive()) {
        const size_t index=visible_[selected_];
        const auto& info=details(index);
        const auto title=info.title.empty()?books_[index].label:info.title;
        drawRowLabel(55,244,23,fitWidth(title,265).c_str());
        drawRowLabel(55,264,23,fitWidth(info.author,265).c_str());
        if(flowProgressIndex_!=index) {
          flowProgressIndex_=index;
          uint32_t ppm=0;
          flowProgressPercent_=books::loadShelfProgress(books_[index].path,ppm)?int(ppm/10000u):-1;
        }
        if(flowProgressPercent_>=0) {
          const auto percent=std::to_string(flowProgressPercent_)+"%";
          drawRowLabel(163,287,22,percent.c_str());
        }
      } else if(rowCount()==0) {
        renderer_.drawText(1,16,102,books::tr(books_.empty()?"No EPUB books found":"No matching books"));
        renderer_.drawText(1,16,144,
            books_.empty()?"Copy EPUBs to /Books":"Tap title to edit search");
      } else {
        const size_t start=(selected_/VISIBLE_ROWS)*VISIBLE_ROWS;
        const size_t first=folderRows();
        for(size_t row=0;row<VISIBLE_ROWS && start+row<rowCount();++row) {
          const size_t filtered=start+row;
          const int y=TOP_Y+static_cast<int>(row)*ROW_H;
          const bool active=filtered==selected_;
          if(filtered<first) {
            drawListRow(y,clippedLabel("[DIR] "+folders_[filtered],31),"",active);
            continue;
          }
          const size_t index=visible_[filtered-first];
          const auto& info=details(index);
          const std::string label=clippedLabel(
              info.title.empty()?books_[index].label:info.title,26);
          drawListRow(y,label,info.author,active);
        }
      }
    }
    // The footer strip holds a notice, or the details screen's action (a
    // tap there downloads); no control hints: the Q2's keys are known.
    const std::string footer=!notice_.empty()?clippedLabel(notice_,24)
      :detailsOpen()?(detailsOnCard_?"Show on card":"Download"):std::string();
    if(!footer.empty()) {
      // Inset from the Q2's rounded bottom corners (MenuLayout::cornerX).
      const int lineH=renderer_.getLineHeight(1);
      const int footerX=books::MenuLayout::cornerX(269+(51-lineH)/2,lineH);
      drawRowLabel(footerX,269,51,footer.c_str());
      books::paintBar(display_,0,269,375,51);  // the footer strip
    }
  }
  // A catalog book: cover left, title and author right, the description
  // under them (the wheel scrolls it); OK downloads.
  void paintDetails() {
    drawRowLabel(16,39,35,"Book details");
    renderer_.drawLine(15,74,359,74,true);
    constexpr int cx=16,cy=80,cw=books::OpdsCover::width,ch=books::OpdsCover::height;
    renderer_.drawRect(cx-1,cy-1,cw+2,ch+2,true);
    if(detailsHasCover_) {
      for(int y=0;y<ch;++y)
        for(int x=0;x<cw;++x)
          if(detailsCover_.inked(x,y))renderer_.drawPixel(cx+x,cy+y,true);
      books::paintPicture(display_,cx,cy,cw,ch);
    } else {
      renderer_.drawLine(cx+14,cy+30,cx+cw-14,cy+30,true);
      renderer_.drawLine(cx+14,cy+40,cx+cw-14,cy+40,true);
    }
    // Title: up to two lines in font 1, then the author in the small face.
    std::string title=detailsEntry_.title;
    int y=78;
    for(int line=0;line<2 && !title.empty();++line) {
      std::string part=title;
      if(line==0 && renderer_.getTextWidth(1,part.c_str())>DETAILS_W) {
        size_t cut=part.rfind(' ');
        while(cut!=std::string::npos && cut>0 &&
              renderer_.getTextWidth(1,part.substr(0,cut).c_str())>DETAILS_W)
          cut=part.rfind(' ',cut-1);
        if(cut!=std::string::npos && cut>0)part=part.substr(0,cut);
      }
      part=fitWidth(part,DETAILS_W);
      renderer_.drawText(1,DETAILS_X,y,part.c_str(),true);
      title=part.size()<title.size() && title.compare(0,part.size(),part)==0?
          trimConnectedLeft(title.substr(part.size())):std::string();
      y+=25;
    }
    int base=y+18;
    if(!detailsEntry_.author.empty() && drawSmallLine(DETAILS_X,base,detailsEntry_.author,DETAILS_W))
      base+=20;
    else base+=4;
    renderer_.drawLine(DETAILS_X,base-12,360,base-12,true);
    const int room=std::max(1,(262-base)/16+1);
    detailsRoom_=static_cast<size_t>(room);
    for(int i=0;i<room && detailsScroll_+static_cast<size_t>(i)<detailsLines_.size();++i)
      (void)drawSmallLine(DETAILS_X,base+i*16,detailsLines_[detailsScroll_+static_cast<size_t>(i)],DETAILS_W);
    if(detailsLines_.size()>static_cast<size_t>(room)) {  // a scroll bar beside the text
      const int top=base-10,height=262-top;
      const int thumb=std::max(8,height*room/static_cast<int>(detailsLines_.size()));
      const int pos=top+static_cast<int>((height-thumb)*static_cast<long>(detailsScroll_)/
          std::max<long>(1,static_cast<long>(detailsLines_.size())-room));
      renderer_.fillRect(364,pos,3,thumb,true);
    }
    if(detailsOnCard_)drawCompactLabel(cx,cy+ch+20,"On the card",140);
  }
  static std::string trimConnectedLeft(std::string text) {
    while(!text.empty() && text.front()==' ')text.erase(0,1);
    return text;
  }
  // The saved catalogs, or the form for one of them.
  void paintServers() {
    const bool form=onlineScreen_==OnlineScreen::Form;
    const char* heading=form?(formIndex_<0?"New server":"Edit server"):
        serverEditMode_?"Choose a server to edit":"Catalog servers";
    drawRowLabel(16,39,35,heading);
    renderer_.drawLine(15,74,359,74,true);
    const size_t rows=form?formRows():serverRows();
    const size_t pick=form?formSelection_:serverSelection_;
    const size_t start=(pick/VISIBLE_ROWS)*VISIBLE_ROWS;
    for(size_t row=0;row<VISIBLE_ROWS && start+row<rows;++row) {
      const size_t index=start+row;
      const int y=TOP_Y+static_cast<int>(row)*ROW_H;
      std::string title,detail;
      if(form) {
        static const char* const labels[6]={"Name","URL","User","Password","Save","Delete server"};
        title=books::tr(labels[index]);
        if(index<4) {
          std::string value=*formField(index);
          if(index==3)value=std::string(std::min<size_t>(value.size(),12),'*');
          if(index==0 && value.empty())value=books::tr("(the host name)");
          else if(value.empty())value=books::tr("(none)");
          detail=value;
        }
        if(index==5 && formDeleteArmed_)title=books::tr("Press again to delete");
      } else if(index<config_.servers.size()) {
        const auto& server=config_.servers[index];
        title=books::opdsServerLabel(server);
        const auto url=books::parseHttpUrl(server.url);
        detail=std::string(url.secure?"https://":"http://")+url.host+
               (server.username.empty()?"":"  ("+server.username+")");
      } else if(serverEditMode_)title=books::tr("Cancel");
      else if(index==config_.servers.size())title=books::tr("+ Add server");
      else title=books::tr("Edit a server");
      drawListRow(y,clippedLabel(title,31),detail,index==pick);
    }
  }
  const books::FlowCover& flowCoverAt(size_t row) {
    const size_t index=visible_[row];
    for(auto& slot:flowSlots_)if(slot.used&&slot.index==index)
      return slot.cover.valid()?slot.cover:fallbackCover_;
    auto* target=&flowSlots_[0];
    for(auto& slot:flowSlots_) {
      if(!slot.used){target=&slot;break;}
      const auto it=std::find(visible_.begin(),visible_.end(),slot.index);
      if(it==visible_.end() || std::abs(float(it-visible_.begin())-flowPosition_)>=books::flow::reach) {
        target=&slot;break;
      }
    }
    target->index=index;target->used=true;target->cover={};
    (void)books::loadFlowCover(books_[index].path,target->cover);
    return target->cover.valid()?target->cover:fallbackCover_;
  }
  void present() {
    paint();
    if(!flowActive()) {
      flowMoving_=false;flowTouch_=false;flowPosition_=float(selected_);
      std::vector<uint32_t>().swap(flowFrame_);std::vector<uint8_t>().swap(flowAlpha_);
      for(auto& slot:flowSlots_)slot={};
      display_.displayBuffer();return;
    }
    if(!flowMoving_ && !flowTouch_)flowPosition_=float(selected_);
    const auto& theme=books::uiTheme();
    const uint32_t paper=theme.active()?theme.box:0xffffff;
    const uint32_t ink=theme.active()?theme.title:0;
    flowFrame_.assign(HalDisplay::PIXELS,0xff000000u|paper);
    flowAlpha_.resize(HalDisplay::PIXELS);
    if(display_.currentAlpha(flowAlpha_.data()))
      for(size_t i=0;i<flowFrame_.size();++i)
        flowFrame_[i]=books::flow::mix(paper,ink,unsigned(flowAlpha_[i])*256/255);
    std::array<size_t,8> rows{};
    size_t count=0;
    const int first=std::max(0,int(std::floor(flowPosition_-books::flow::reach)));
    for(size_t row=size_t(first);row<visible_.size()&&count<rows.size();++row) {
      if(float(row)-flowPosition_>=books::flow::reach)break;
      if(std::abs(float(row)-flowPosition_)<books::flow::reach)rows[count++]=row;
    }
    std::sort(rows.begin(),rows.begin()+count,[&](size_t a,size_t b){
      return std::abs(float(a)-flowPosition_)>std::abs(float(b)-flowPosition_);
    });
    for(size_t i=0;i<count;++i) {
      const size_t row=rows[i];
      books::flow::draw(flowFrame_.data(),flowCoverAt(row),float(row)-flowPosition_,paper);
    }
    if(!display_.presentColorFrame(flowFrame_.data(),flowFrame_.size()))display_.displayBuffer();
  }
  void animationTick() {
    if(!animating())return;
    const auto now=ReaderClock::now();
    const float ms=std::chrono::duration<float,std::milli>(now-flowAt_).count();
    if(ms<16)return;
    flowAt_=now;
    flowPosition_+=(float(selected_)-flowPosition_)*(1.f-std::exp(-ms/55.f));
    if(std::abs(float(selected_)-flowPosition_)<.015f) {
      flowPosition_=float(selected_);flowMoving_=false;
    }
    present();
  }
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

// RD-01 go-to-percent dialog input, shared by the card app and the Books
// handoff reader. Returns true while the dialog stays open.
enum class PercentResult { Open, Cancelled, Moved, Failed };
PercentResult handlePercentInput(Reader& reader,books::LogicalAction action,
                                 const books::PhysicalEvent& event,int& percent) {
  const bool wheel=event.kind==books::PhysicalKind::WheelStep;
  if(action==books::LogicalAction::Back ||
     (event.kind==books::PhysicalKind::TouchUp && event.y<70))
    return PercentResult::Cancelled;
  if(action==books::LogicalAction::Previous || action==books::LogicalAction::Next) {
    const int step=wheel?1:10;
    percent=std::clamp(percent+(action==books::LogicalAction::Next?step:-step),0,100);
    reader.showPercentDialog(percent);
    return PercentResult::Open;
  }
  if(event.kind==books::PhysicalKind::TouchUp && event.y>=170 && event.y<=190) {
    percent=std::clamp((event.x-30)*100/315,0,100);
    reader.showPercentDialog(percent);
    return PercentResult::Open;
  }
  if(action==books::LogicalAction::Confirm || event.kind==books::PhysicalKind::TouchUp)
    return reader.goToBookPercent(percent)?PercentResult::Moved:PercentResult::Failed;
  return PercentResult::Open;
}
// RD-02 auto page turn. Called on every idle wake: arms the timer, turns the
// page when it is due, and returns the deadline to wait for (max() = none).
ReaderClock::time_point autoTurnTick(Reader& reader,bool allowed,ReaderClock::time_point& next) {
  const int seconds=reader.preferences().autoTurnSeconds;
  const auto none=ReaderClock::time_point::max();
  if(!allowed || seconds<=0){next=none;return none;}
  const auto now=ReaderClock::now();
  if(next==none){next=now+std::chrono::seconds(seconds);return next;}
  if(now<next)return next;
  if(reader.move(1)) {
    reader.present();
    next=ReaderClock::now()+std::chrono::seconds(seconds);
  } else if(reader.atBookEnd()) {
    next=none-std::chrono::hours(1);  // stop at the last page until input
  } else next=ReaderClock::now()+std::chrono::seconds(1);  // still laying out
  return next;
}

int runCardApp(const std::string& bookDir) {
  if(!installExitHandlers())return 5;
  SessionProfile profile;
  // Downloads cut off by a kill leave an unlocked .part each: remove them.
  if(const int stale=books::sweepStaleOpdsDownloads(bookDir))
    std::fprintf(stderr,"Books: removed %d interrupted download(s)\n",stale);
  std::vector<ShelfEntry> books=scanCardBooks(bookDir);
  if(interrupted)return 0;
  auto recent=books::loadRecentBooks();
  auto sortMode=books::ShelfSortMode::Title;
  books::sortShelfBooks(books,sortMode,recent);
  books::Q2SocketInput input;
  if(!input.open()) {
    std::fputs("Books card: cannot bind /tmp/q2-books.input.sock\n",stderr);
    return 5;
  }
  size_t selection=books::lastOpenedIndex(books,recent);
  std::unique_ptr<CardShelf> shelf(new CardShelf(books,selection,selection,sortMode,bookDir));
  if(!shelf->open())return 4;
  if(interrupted)return 0;
  shelf->present();
  books::Q2Controls controls(loadWheelSensitivity());
  std::unique_ptr<Reader> reader;
  bool typeMenu=false;
  bool navigationMenu=false;
  bool linkMenu=false;
  bool selectionChoosing=false;
  bool selectionExtending=false;
  bool selectionActions=false;
  bool dictionaryOverlay=false;
  bool footnoteOverlay=false;
  bool percentDialog=false;
  bool syncDialog=false;
  int syncChoice=0;
  bool aboutShown=false;
  bool dictionaryList=false;
  int dictionarySelection=0;
  int percentValue=0;
  // RD-02 auto page turn: the next turn is due at this time; reset by input.
  auto nextAutoTurn=ReaderClock::time_point::max();
  std::string footnoteHref;
  int selectionAction=0;
  bool touchPressed=false;
  int touchStartX=0,touchStartY=0;
  std::chrono::steady_clock::time_point touchStartTime{};
  bool showingBookmarks=false;
  int navigationSelection=0;
  int typeSelection=0;
  auto nextBuildTick=std::chrono::steady_clock::now();
  auto nextStatusCheck=std::chrono::steady_clock::now();
  size_t nextShelfIndex=0;
  auto nextShelfIndexTick=std::chrono::steady_clock::now()+
      std::chrono::milliseconds(800);
  // A wheel datagram can carry several ticks (MappedEvent::repeat): play each as
  // its own step, before reading the next datagram.
  books::LogicalAction pendingAction=books::LogicalAction::None;
  int pendingRepeat=0;
  while(!interrupted) {
    if(reader)reader->flushPosition(false);
    books::PhysicalEvent event{};
    if(pendingRepeat==0 && !input.receive(event)) {
      const auto now=std::chrono::steady_clock::now();
      // Work only while idle on the bookshelf, not when reading or during
      // input handling. One bounded OPF/thumbnail attempt per idle tick;
      // never parse chapter pages or write an index on every wheel turn.
      if(!reader && shelf && shelf->animating())shelf->animationTick();
      if(!reader && shelf && !shelf->animating() && nextShelfIndex<books.size() && now>=nextShelfIndexTick) {
        const std::string& book=books[nextShelfIndex++].path;
        const auto result=books::indexShelfBook(book);
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
      const bool canAutoTurn=reader && !typeMenu && !navigationMenu && !linkMenu &&
          !selectionChoosing && !selectionActions && !dictionaryOverlay &&
          !footnoteOverlay && !percentDialog;
      // A new minute or battery level: show the footer again, but only
      // while a page shows with nothing over it.
      const bool pageShowing=canAutoTurn && !syncDialog && !aboutShown && !dictionaryList &&
          !selectionExtending;
      if(pageShowing && now>=nextStatusCheck) {
        nextStatusCheck=now+std::chrono::seconds(1);
        if(reader->statusChanged())reader->present();
      }
      const auto beforeTurn=nextAutoTurn;
      const auto turnDue=reader?autoTurnTick(*reader,canAutoTurn,nextAutoTurn):
                                ReaderClock::time_point::max();
      if(reader && beforeTurn!=ReaderClock::time_point::max() && nextAutoTurn!=beforeTurn &&
         beforeTurn<=ReaderClock::now())continue;  // a page was turned: loop again
      auto deadline=reader?reader->nextPositionFlush():ReaderClock::time_point::max();
      deadline=std::min(deadline,turnDue);
      if(!reader && shelf && shelf->animating())deadline=std::min(deadline,ReaderClock::now()+std::chrono::milliseconds(16));
      if(!reader && shelf && !shelf->animating() && nextShelfIndex<books.size())deadline=std::min(deadline,nextShelfIndexTick);
      if(canPrefetch && reader->needsPagePrefetch())deadline=std::min(deadline,nextBuildTick);
      if(waitForReaderInput(input,deadline)<0) {
        std::fputs("Books: input socket poll failed\n",stderr);
        return 5;
      }
      continue;
    }
    if(interrupted)break;
    books::LogicalAction action;
    if(pendingRepeat>0) {
      --pendingRepeat;
      event=books::PhysicalEvent{};
      event.kind=books::PhysicalKind::WheelStep;
      action=pendingAction;
    } else {
      const auto mapped=controls.translate(event);
      action=mapped.action;
      if(mapped.repeat>1){pendingAction=mapped.action;pendingRepeat=mapped.repeat-1;}
    }
    nextShelfIndexTick=std::chrono::steady_clock::now()+
        std::chrono::milliseconds(800);
    nextBuildTick=std::chrono::steady_clock::now()+
                  std::chrono::milliseconds(300);
    nextAutoTurn=ReaderClock::time_point::max();  // any input restarts the interval
    if(reader) {
      if(syncDialog) {  // M4: which reading position to keep
        int pick=-1;
        if(action==books::LogicalAction::Back)pick=2;
        else if(action==books::LogicalAction::Next || action==books::LogicalAction::Previous) {
          syncChoice=std::clamp(syncChoice+(action==books::LogicalAction::Next?1:-1),0,2);
          reader->showSyncConflict(syncChoice);
          continue;
        } else if(action==books::LogicalAction::Confirm)pick=syncChoice;
        else if(event.kind==books::PhysicalKind::TouchUp && event.y>=146 && event.y<254)
          pick=(event.y-146)/36;
        if(pick<0)continue;
        syncDialog=false;
        const auto conflict=reader->syncConflict();
        reader->clearSyncConflict();
        bool moved=false;
        if(pick==0)moved=reader->applyRemoteProgress(conflict.remote);
        else if(pick==1)(void)reader->pushReadingSync(true);
        if(moved){typeMenu=false;reader->present();}
        else reader->showTypographyMenu(typeSelection);
        continue;
      }
      if(percentDialog) {
        const auto result=handlePercentInput(*reader,action,event,percentValue);
        if(result==PercentResult::Open)continue;
        percentDialog=false;
        if(result==PercentResult::Moved) {
          typeMenu=false;
          reader->present();
        } else reader->showTypographyMenu(typeSelection);
        continue;
      }
      if(footnoteOverlay) {
        if(action==books::LogicalAction::Back ||
           (event.kind==books::PhysicalKind::TouchUp && event.x<187)) {
          footnoteOverlay=false;
          reader->present();
        } else if(action==books::LogicalAction::Confirm ||
                  event.kind==books::PhysicalKind::TouchUp) {
          footnoteOverlay=false;
          const bool jumped=reader->followLink(footnoteHref);
          if(jumped) {
            linkMenu=false;typeMenu=false;reader->present();
          } else reader->present();
        }
        continue;
      }
      if(dictionaryOverlay) {
        if(action==books::LogicalAction::Next ||
           action==books::LogicalAction::Previous) {
          reader->showDictionaryDefinition(action==books::LogicalAction::Next?1:-1);
        } else if(action==books::LogicalAction::Back ||
           action==books::LogicalAction::Confirm ||
           event.kind==books::PhysicalKind::TouchUp) {
          dictionaryOverlay=false;
          selectionActions=true;
          reader->showTextSelectionMenu(selectionAction);
        }
        continue;
      }
      if(selectionActions) {
        if(action==books::LogicalAction::Back) {
          selectionActions=false;reader->discardSelection();reader->present();
        } else if(action==books::LogicalAction::Next ||
                  action==books::LogicalAction::Previous) {
          selectionAction=(selectionAction+
              (action==books::LogicalAction::Next?1:3))%4;
          reader->showTextSelectionMenu(selectionAction);
        } else if(action==books::LogicalAction::Confirm ||
                  event.kind==books::PhysicalKind::TouchUp) {
          if(event.kind==books::PhysicalKind::TouchUp) {
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
        if(action==books::LogicalAction::Back) {
          selectionChoosing=false;selectionExtending=false;
          reader->discardSelection();reader->present();
        } else if(action==books::LogicalAction::Next ||
                  action==books::LogicalAction::Previous) {
          (void)reader->stepSelection(
              action==books::LogicalAction::Next?1:-1,
              selectionExtending);
          reader->showSelectionPreview(selectionExtending);
        } else if(action==books::LogicalAction::Confirm) {
          if(!selectionExtending) {
            selectionExtending=true;
            reader->showSelectionPreview(true);
          } else {
            selectionChoosing=false;selectionExtending=false;
            selectionActions=true;selectionAction=0;
            reader->showTextSelectionMenu(0);
          }
        } else if(event.kind==books::PhysicalKind::TouchUp) {
          if(reader->selectTouchWord(event.x,event.y,selectionExtending))
            reader->showSelectionPreview(selectionExtending);
        }
        continue;
      }
      if(linkMenu) {
        const auto links=reader->pageLinks();
        const int count=static_cast<int>(links.size());
        if(action==books::LogicalAction::Back) {
          linkMenu=false;
          reader->showTypographyMenu(typeSelection);
        } else if(action==books::LogicalAction::Next ||
                  action==books::LogicalAction::Previous) {
          if(count)navigationSelection=std::clamp(
              navigationSelection+
              (action==books::LogicalAction::Next?1:-1),0,count-1);
          reader->showLinksMenu(navigationSelection);
        } else if(action==books::LogicalAction::Confirm ||
                  event.kind==books::PhysicalKind::TouchUp) {
          if(event.kind==books::PhysicalKind::TouchUp)
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
        if(action==books::LogicalAction::Back) {
          navigationMenu=false;
          reader->showTypographyMenu(typeSelection);
        } else if(action==books::LogicalAction::Next ||
                  action==books::LogicalAction::Previous) {
          if(count)navigationSelection=std::clamp(
            navigationSelection+(action==books::LogicalAction::Next?1:-1),
            0,count-1);
          reader->showNavigationMenu(showingBookmarks,navigationSelection);
        } else if(action==books::LogicalAction::Confirm ||
                  event.kind==books::PhysicalKind::TouchUp) {
          if(event.kind==books::PhysicalKind::TouchUp) {
            const int tapped=reader->navRowAt(showingBookmarks,navigationSelection,event.y);
            if(tapped<0)continue;  // the bar, the footer or below the list
            navigationSelection=tapped;
          }
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
      if(typeMenu && dictionaryList) {
        const int count=reader->dictionaryCount();
        if(action==books::LogicalAction::Back) {
          dictionaryList=false;reader->showTypographyMenu(typeSelection);
        } else if((action==books::LogicalAction::Next || action==books::LogicalAction::Previous) && count) {
          dictionarySelection=(dictionarySelection+(action==books::LogicalAction::Next?1:count-1))%count;
          reader->showDictionaryList(dictionarySelection);
        } else if(action==books::LogicalAction::Confirm || event.kind==books::PhysicalKind::TouchUp) {
          if(event.kind==books::PhysicalKind::TouchUp) {
            using L=books::MenuLayout;
            if(event.y<L::kBarH){dictionaryList=false;reader->showTypographyMenu(typeSelection);continue;}
            const int tapped=L::itemAt(L::first(dictionarySelection,count),count,event.y);
            if(tapped<0)continue;
            dictionarySelection=tapped;
          }
          (void)reader->toggleDictionary(dictionarySelection);
          reader->showDictionaryList(dictionarySelection);
        }
        continue;
      }
      if(typeMenu && aboutShown) {
        if(action==books::LogicalAction::Back || action==books::LogicalAction::Confirm ||
           action==books::LogicalAction::Next || action==books::LogicalAction::Previous ||
           event.kind==books::PhysicalKind::TouchUp) {
          aboutShown=false;reader->showTypographyMenu(typeSelection);
        }
        continue;
      }
      if(typeMenu) {
        if(action==books::LogicalAction::Back) {
          typeMenu=false;
          reader->present();
        } else if(action==books::LogicalAction::Previous ||
                  action==books::LogicalAction::Next) {
          typeSelection=(typeSelection+
              (action==books::LogicalAction::Next?1:kTypographyItems-1))%kTypographyItems;
          reader->showTypographyMenu(typeSelection);
        } else if(action==books::LogicalAction::Confirm ||
                  event.kind==books::PhysicalKind::TouchUp) {
          if(event.kind==books::PhysicalKind::TouchUp) {
            const int tapped=books::MenuLayout::itemAt(
                Reader::menuFirstRow(typeSelection,kTypographyItems),kTypographyItems,event.y);
            if(tapped<0)continue;  // the bar or the footer
            typeSelection=tapped;
          }
          if(typeSelection==19) {
            aboutShown=true;
            reader->showAbout();
            continue;
          }
          if(typeSelection==20) {
            dictionaryList=true;dictionarySelection=0;
            reader->showDictionaryList(0);
            continue;
          }
          if(typeSelection==21) {
            (void)reader->exportNotes();
            reader->showTypographyMenu(typeSelection);
            continue;
          }
          if(typeSelection==18) {
            percentDialog=true;
            percentValue=reader->bookPercent();
            reader->showPercentDialog(percentValue);
            continue;
          } else if(typeSelection==16 || typeSelection==17) {
            (void)reader->adjustPreference(typeSelection);
          } else if(typeSelection==8) {
            (void)reader->rememberPage();
          } else if(typeSelection==14 || typeSelection==15) {
            reader->flushPosition();
            const bool done=typeSelection==14?reader->pushReadingSync():reader->pullReadingSync();
            if(reader->syncConflict().kind!=books::SyncConflict::None) {
              syncDialog=true;syncChoice=0;
              reader->showSyncConflict(syncChoice);
              continue;
            }
            if(done && typeSelection==15) {
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
              next.family=books::nextInstalledTypographyFamily(next);
              next.size=books::nearestTypographySize(next);
            } else next.size=books::nextInstalledTypographySize(next);
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
      if(event.kind==books::PhysicalKind::TouchDown) {
        touchPressed=true;touchStartX=event.x;touchStartY=event.y;
        touchStartTime=std::chrono::steady_clock::now();
        continue;
      }
      if(event.kind==books::PhysicalKind::TouchUp && touchPressed) {
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
      if(action==books::LogicalAction::Confirm ||
         (event.kind==books::PhysicalKind::TouchUp && event.y<45)) {
        typeMenu=true;
        typeSelection=0;
        reader->showTypographyMenu(typeSelection);
        continue;
      }
      if(action==books::LogicalAction::Back) {
        (void)reader->cacheShelfCover();
        reader.reset(); // close book/cache BEFORE reopening the card shelf
        books::sortShelfBooks(books,sortMode,recent);
        selection=books::lastOpenedIndex(books,recent);
        shelf.reset(new CardShelf(books,selection,selection,sortMode,bookDir));
        if(!shelf->open())return 6;
        shelf->present();
        continue;
      }
      bool changed=false;
      if(action==books::LogicalAction::Next) changed=reader->move(1);
      if(action==books::LogicalAction::Previous)changed=reader->move(-1);
      if(event.kind==books::PhysicalKind::TouchUp) {
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
    if(shelf->onlineSearching() || shelf->fieldEditing()) {
      if(action==books::LogicalAction::Back)shelf->endSearch();
      else if(action==books::LogicalAction::Previous)(void)shelf->change(-1);
      else if(action==books::LogicalAction::Next)(void)shelf->change(1);
      else if(action==books::LogicalAction::Confirm)(void)shelf->applySearchChoice();
      else if(event.kind==books::PhysicalKind::TouchUp)(void)shelf->tap(event.x,event.y);
      if(shelf->takeSearchSubmit()) {
        shelf->setNotice("Searching...");
        shelf->present();
        (void)shelf->submitOnlineSearch();  // on failure the keyboard stays, with the reason
      }
      shelf->present();
      continue;
    }
    if(shelf->serverScreen()) {
      const bool touch=event.kind==books::PhysicalKind::TouchUp;
      const auto result=shelf->serverInput(action,event.x,event.y,touch);
      if(result==CardShelf::ServerAction::Open) {
        shelf->setNotice("Loading catalog...");
        shelf->present();
        (void)shelf->openServer();  // on failure the list stays, with the reason
      }
      if(result!=CardShelf::ServerAction::None)shelf->present();
      continue;
    }
    if(shelf->detailsOpen()) {
      const bool touch=event.kind==books::PhysicalKind::TouchUp;
      if(action==books::LogicalAction::Back || (touch && event.y<73)) {
        shelf->closeDetails();shelf->present();continue;
      }
      if(action==books::LogicalAction::Next || action==books::LogicalAction::Previous) {
        if(shelf->change(action==books::LogicalAction::Next?1:-1))shelf->present();
        continue;
      }
      // OK, or a tap on the footer (where "Download" is written).
      if(action!=books::LogicalAction::Confirm && !(touch && event.y>=269))continue;
    }
    if(shelf->onlineMode()) {
      if(action==books::LogicalAction::Back) {
        shelf->returnFromOnline();
        shelf->present();continue;
      }
      if(action==books::LogicalAction::Next ||
         action==books::LogicalAction::Previous) {
        if(shelf->change(action==books::LogicalAction::Next?1:-1))
          shelf->present();
        continue;
      }
      if(event.kind==books::PhysicalKind::TouchUp && event.y<73) {
        shelf->returnFromOnline();shelf->present();continue;
      }
      bool activate=action==books::LogicalAction::Confirm;
      if(event.kind==books::PhysicalKind::TouchUp && !shelf->detailsOpen()) {
        shelf->onlineTouch(event.x,event.y);
        activate=true;
      }
      if(event.kind==books::PhysicalKind::TouchUp && shelf->detailsOpen())activate=true;
      if(!activate || (!shelf->detailsOpen() && !shelf->onlineHasSelection()))continue;
      const auto selected=shelf->detailsOpen()?shelf->detailsEntry():shelf->onlineEntry();
      if(selected.kind==books::OpdsEntry::Kind::Epub && !shelf->detailsOpen()) {
        shelf->setNotice("Loading...");
        shelf->present();
        shelf->openDetails(selected,bookDir);  // its cover, from the card or the server
        shelf->present();
        continue;
      }
      if(selected.kind==books::OpdsEntry::Kind::Search) {
        shelf->enterOnlineSearch();
        shelf->present();
        continue;
      }
      if(selected.kind==books::OpdsEntry::Kind::Catalog ||
         selected.kind==books::OpdsEntry::Kind::More) {
        shelf->setNotice("Loading catalog...");
        shelf->present();
        (void)shelf->loadOnlinePage(selected.href,true);
        shelf->present();
        continue;
      }
      shelf->setNotice("Downloading EPUB...");
      shelf->present();
      std::string saved,error;
      struct stat existing{};
      const std::string destination=books::opdsDestination(bookDir,selected);
      const bool already=!destination.empty() && ::lstat(destination.c_str(),&existing)==0;
      if(!books::opdsDownloadEpub(bookDir,selected,saved,error,shelf->server())) {
        shelf->setNotice(error.empty()?"Download failed":error);
        shelf->present();continue;
      }
      // The reader only sees fully atomically renamed EPUB ZIPs. Rescan the
      // bounded library after a download, never while a .part is in flight.
      books=scanCardBooks(bookDir);
      books::sortShelfBooks(books,sortMode,recent);
      auto found=std::find_if(books.begin(),books.end(),
          [&](const ShelfEntry& entry){return entry.path==saved;});
      selection=found==books.end()?0:
          static_cast<size_t>(found-books.begin());
      shelf.reset(new CardShelf(books,selection,selection,sortMode,bookDir));
      if(!shelf->open())return 6;
      shelf->enterBrowser();
      shelf->setNotice(already?"Already on the card":"EPUB downloaded to Books/Online");
      shelf->present();
      nextShelfIndex=0;
      continue;
    }
    if(shelf->searching()) {
      if(action==books::LogicalAction::Back)
        shelf->endSearch();
      else if(action==books::LogicalAction::Previous)
        (void)shelf->change(-1);
      else if(action==books::LogicalAction::Next)
        (void)shelf->change(1);
      else if(action==books::LogicalAction::Confirm)
        (void)shelf->applySearchChoice();
      else if(event.kind==books::PhysicalKind::TouchUp)
        (void)shelf->tap(event.x,event.y);
      shelf->present();
      continue;
    }
    if(action==books::LogicalAction::Back) {
      if(shelf->browsing() && shelf->folderUp()) {shelf->present();continue;}
      if(shelf->browsing()) {shelf->goHome();shelf->present();continue;}
      break;
    }
    if(shelf->flowTouchEvent(event))continue;
    if(action==books::LogicalAction::Previous && shelf->change(-1))shelf->present();
    if(action==books::LogicalAction::Next && shelf->change(1))shelf->present();
    bool activate=action==books::LogicalAction::Confirm;
    if(event.kind==books::PhysicalKind::TouchUp && shelf->browsing() &&
       event.y>=39 && event.y<74 && event.x>=190) {
      (void)shelf->enterOnline();
      shelf->present();
      continue;
    }
    if(event.kind==books::PhysicalKind::TouchUp)activate=shelf->tap(event.x,event.y);
    if(!activate) {if(event.kind==books::PhysicalKind::TouchUp && shelf->animating())shelf->present();continue;}
    if(shelf->viewChosen()) {shelf->toggleView();shelf->present();continue;}
    if(shelf->searching()) {shelf->present();continue;}
    if(shelf->onlineChosen()) {
      (void)shelf->enterOnline();
      shelf->present();
      continue;
    }
    if(shelf->sortChosen()) {
      sortMode=sortMode==books::ShelfSortMode::Title?
          books::ShelfSortMode::Recent:
          (sortMode==books::ShelfSortMode::Recent?
          books::ShelfSortMode::RecentlyAdded:
          sortMode==books::ShelfSortMode::RecentlyAdded?
          books::ShelfSortMode::Folders:books::ShelfSortMode::Title);
      books::sortShelfBooks(books,sortMode,recent);
      selection=books::lastOpenedIndex(books,recent);
      shelf.reset(new CardShelf(books,selection,selection,sortMode,bookDir));
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
    if(shelf->folderSelected()) {shelf->openSelectedFolder();shelf->present();continue;}
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
      (void)books::saveShelfDetails(chosen,reader->shelfDetails());
      if(books::recordRecentBook(chosen))
        recent=books::loadRecentBooks();
    }
  }
  return 0;
}

void usage() {
  std::fputs(
    "Usage:\n"
    "  books-reader --validate <book.epub>       (no framebuffer)\n"
    "  books-reader --walk <book.epub>           (test pagination)\n"
    "  books-reader --bench <book.epub>          (latency & memory)\n"
    "  books-reader --list-books <directory>     (card scanner test)\n"
    "  books-reader --test-page-links <book.epub> (real link regression)\n"
    "  books-reader --test-progress <book>         (go-to-percent regression)\n"
    "  books-reader --define <word>               (dictionary lookup)\n"
    "  books-reader --index-book <book.epub>     (OPF/cover index test)\n"
    "  books-reader --card-preview <dir> <out.pgm> (menu raster test)\n"
    "  books-reader --card-search-preview <dir> <out.pgm> (search UI test)\n"
    "  books-reader (no args)                    (Q2 card app)\n"
    "  books-reader --render <book.epub> <page.pgm> (no framebuffer)\n"
    "  books-reader <book.epub>                  (Q2 Pod Books handoff)\n"
    "On Q2, only run interactively after Q2 Pod yields /dev/fb0.\n",stderr);
}
} // namespace

int main(int argc,char** argv) {
  // Menus follow Q2 Pod's language; a translation the menu font can't draw
  // stays English rather than showing boxes.
  books::setUiGlyphCheck([](uint32_t cp) {
    // Q2 Pod's own faces (CJK in stock default.ttf, Thai in a language
    // pack's font), else the built-in menu font.
    books::UiText::instance().setThemeFace(books::uiTextFace());
    if(books::UiText::instance().has(cp))return true;
    static const EpdFont menuFont(&notosans_12_regular);
    return menuFont.getGlyph(cp)!=nullptr;
  });
  books::httpAbortFlag=&interrupted;  // SIGTERM stops a running transfer
  bookslayout::layoutAbortFlag=&interrupted;  // and an overlong word's layout
  // Bring the user's files over from the app's former folder, once, before
  // anything reads state (not when a test points the state elsewhere).
  const char* stateOverride=std::getenv("BOOKS_STATE_DIR");
  const bool onCard=!(stateOverride && *stateOverride);
  if(argc==2 && (std::strcmp(argv[1],"--version")==0 || std::strcmp(argv[1],"--about")==0)) {
    std::printf("Books %s\n%s\n",BOOKS_VERSION,books::kAboutText);
    return 0;
  }
  // Card apps start with NO book argument; the on-device picker owns launch.
  if(argc==1) {
    if(onCard)(void)books::migrateLegacyState();
    return runCardApp("/mnt/mmc/Books");
  }
  if(argc==3 && std::strcmp(argv[1],"--opds-probe")==0) {
    // The credentials of the saved server on the same origin, if any.
    books::OpdsServer server;
    for(const auto& saved:books::loadConnectedConfig().servers)
      if(books::sameHttpOrigin(saved.url,argv[2])){server=saved;break;}
    const auto catalog=books::fetchOpdsFeed(argv[2],server);
    if(!catalog.error.empty()) {
      std::fprintf(stderr,"Books OPDS request: %s\n",catalog.error.c_str());
      return 8;
    }
    std::printf("Books OPDS: %s, entries=%zu%s\n",
                catalog.title.c_str(),catalog.entries.size(),
                catalog.offline?" (offline copy)":"");
    for(const auto& entry:catalog.entries)
      std::printf("%s: %s\n",
        entry.kind==books::OpdsEntry::Kind::Epub?"EPUB":"CATALOG",
        entry.title.c_str());
    return 0;
  }
  if((argc==3 && std::strcmp(argv[1],"--opds-pages")==0) ||
     (argc==4 && std::strcmp(argv[1],"--opds-search")==0)) {
    // M4: follow rel="next" (at most 50 pages, no page twice), or search.
    std::string url=argv[2];
    if(argc==4) {
      const auto root=books::fetchOpdsFeed(url);
      if(!root.error.empty())return 8;
      std::string error;
      url=books::opdsSearchUrl(root,argv[3],error);
      if(url.empty()){std::fprintf(stderr,"Books OPDS search: %s\n",error.c_str());return 11;}
      std::printf("Books OPDS search URL: %s\n",url.c_str());
    }
    std::vector<std::string> seen;
    size_t total=0;
    for(int page=0;page<50 && !url.empty();++page) {
      if(std::find(seen.begin(),seen.end(),url)!=seen.end()) {
        std::puts("Books OPDS: next link loops; stopped");break;
      }
      seen.push_back(url);
      const auto feed=books::fetchOpdsFeed(url);
      if(!feed.error.empty())return 8;
      total+=feed.entries.size();
      std::printf("Books OPDS page %d: %zu entries%s\n",page+1,feed.entries.size(),
                  feed.nextHref.empty()?"":" (next)");
      for(const auto& e:feed.entries)std::printf("  %s\n",e.title.c_str());
      url=feed.nextHref;
    }
    std::printf("Books OPDS total: %zu entries in %zu pages\n",total,seen.size());
    return 0;
  }
  // M4: the first EPUB entry of a feed: its description and its cover
  // (fetched, or the card's copy), written as a PGM.
  if(argc==4 && std::strcmp(argv[1],"--opds-cover")==0) {
    books::OpdsServer server;
    for(const auto& saved:books::loadConnectedConfig().servers)
      if(books::sameHttpOrigin(saved.url,argv[2])){server=saved;break;}
    const auto feed=books::fetchOpdsFeed(argv[2],server);
    for(const auto& entry:feed.entries) {
      if(entry.kind!=books::OpdsEntry::Kind::Epub)continue;
      std::printf("title: %s\nsummary: %s\nthumbnail: %s\n",entry.title.c_str(),
                  entry.summary.c_str(),entry.thumbnail.c_str());
      books::OpdsCover cover;
      if(!books::fetchOpdsCover(entry,server,cover)){std::puts("cover: none");return 9;}
      FILE* out=std::fopen(argv[3],"wb");
      if(!out)return 9;
      std::fprintf(out,"P5\n%d %d\n255\n",books::OpdsCover::width,books::OpdsCover::height);
      for(int y=0;y<books::OpdsCover::height;++y)
        for(int x=0;x<books::OpdsCover::width;++x)std::fputc(cover.inked(x,y)?0:255,out);
      std::fclose(out);
      std::puts("cover: ok");
      return 0;
    }
    std::fprintf(stderr,"no EPUB entry: %s\n",feed.error.c_str());
    return 8;
  }
  if(argc==4 && std::strcmp(argv[1],"--opds-download")==0) {
    // As in the app: SIGTERM stops the transfer and removes its .part.
    if(!installExitHandlers())return 5;
    auto catalog=books::fetchOpdsFeed(argv[2]);
    if(!catalog.error.empty())return 8;
    for(const auto& entry:catalog.entries) {
      if(entry.kind!=books::OpdsEntry::Kind::Epub)continue;
      std::string saved,error;
      if(!books::opdsDownloadEpub(argv[3],entry,saved,error)) {
        std::fprintf(stderr,"Books download: %s\n",error.c_str());
        return 9;
      }
      std::printf("Books OPDS downloaded: %s\n",saved.c_str());
      return 0;
    }
    return 10;
  }
  if(argc==3 && std::strcmp(argv[1],"--koreader-hash")==0) {
    const auto md5=books::koReaderPartialMd5(argv[2]);
    if(md5.empty())return 9;
    std::printf("Books KOReader partial MD5: %s\n",md5.c_str());
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-koreader-sync")==0) {
    Reader reader(argv[2]);
    if(!reader.open(true))return 3;
    if(reader.chapterCount()>1 && !reader.goToChapter(1))return 4;
    if(!reader.pushReadingSync())return 5;
    if(!reader.pullReadingSync())return 6;
    std::puts("Books KOReader XPath progress roundtrip: PASS");
    return 0;
  }
  // M4 sync conflicts, one step per run (positions persist in the state
  // folder): --test-sync <epub> <chapter> push|pull [force]
  if((argc==5 || argc==6) && std::strcmp(argv[1],"--test-sync")==0) {
    Reader reader(argv[2]);
    if(!reader.open(true))return 3;
    const int chapter=std::atoi(argv[3]);
    if(chapter>=0 && !reader.goToChapter(chapter))return 4;
    const bool force=argc==6 && std::strcmp(argv[5],"force")==0;
    const bool push=std::strcmp(argv[4],"push")==0;
    const bool ok=push?reader.pushReadingSync(force):reader.pullReadingSync(force);
    const auto& c=reader.syncConflict();
    if(c.kind!=books::SyncConflict::None) {
      std::printf("conflict %s server=%.3f local=%.3f device=%s\n",
                  c.kind==books::SyncConflict::ServerAhead?"ahead":"behind",
                  static_cast<double>(c.remote.percentage),static_cast<double>(c.local),c.remote.device.c_str());
      return 0;
    }
    std::printf("%s %s percent=%d\n",ok?"ok":"failed",reader.notice().c_str(),reader.bookPercent());
    return ok?0:7;
  }
  if(argc==3 && std::strcmp(argv[1],"--index-book")==0) {
    // Host/QEMU regression: metadata and an EPUB3 cover-image must become
    // readable cached shelf files WITHOUT constructing a Reader or Section.
    const auto result=books::indexShelfBook(argv[2]);
    books::ShelfDetails details;
    books::ShelfCover cover;
    const bool haveMetadata=books::loadShelfDetails(argv[2],details);
    const bool haveCover=books::loadShelfCover(argv[2],cover);
    std::printf("Books shelf index: metadata=%d cover=%d updated=%d title=%s author=%s\n",
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
      std::puts("Books XHTML footnote popover: PASS");
      return 0;
    }
    std::puts("Books XHTML footnote noteref missing");
    return 7;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-clippings")==0) {
    // M3: a highlight's words and a bookmark reach Clippings/<title>.txt.
    Reader reader(argv[2]);
    if(!reader.open() || !reader.beginTextSelection())return 7;
    if(!reader.stepSelection(1,true))return 8;
    const std::string words=reader.selectedText();
    if(words.empty() || !reader.saveSelectionHighlight())return 9;
    if(!reader.rememberPage())return 10;
    if(!reader.exportNotes())return 11;
    const char* state=std::getenv("BOOKS_STATE_DIR");
    if(!state)return 12;
    std::string all;
    for(const auto& name:{std::string("/Clippings")}) {
      DIR* d=opendir((std::string(state)+name).c_str());
      if(!d)return 13;
      for(dirent* e;(e=readdir(d))!=nullptr;) {
        if(e->d_name[0]=='.')continue;
        FILE* f=std::fopen((std::string(state)+name+"/"+e->d_name).c_str(),"rb");
        if(!f)continue;
        char buf[4096];size_t n;
        while((n=std::fread(buf,1,sizeof buf,f))>0)all.append(buf,n);
        std::fclose(f);
      }
      closedir(d);
    }
    std::printf("%s",all.c_str());
    if(all.find("\""+words+"\"")==std::string::npos || all.find("Bookmark")==std::string::npos)return 14;
    std::puts("Books clippings export: PASS");
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-selection-geometry")==0) {
    // A one-word page: the word's touch box must hold the glyphs' ink and a
    // press on the ink must pick that word (it picked the line below).
    Reader reader(argv[2]);
    if(!reader.open() || !reader.beginTextSelection())return 7;
    int x0=0,y0=0,x1=0,y1=0;
    if(!reader.inkBounds(x0,y0,x1,y1))return 8;
    const auto& words=reader.selectableWordsForTest();
    if(words.size()!=1)return 9;
    const auto& w=words[0];
    std::printf("ink %d,%d-%d,%d  box %d,%d %dx%d\n",x0,y0,x1,y1,w.x,w.y,w.width,w.height);
    if(y0<w.y || y1>w.y+w.height || x0<w.x-2 || x1>w.x+w.width+2)return 10;
    if(books::hitSelectableWord(words,(x0+x1)/2,(y0+y1)/2)!=0)return 11;
    std::puts("Books selection touch boxes on glyphs: PASS");
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-selection")==0) {
    Reader reader(argv[2]);
    if(!reader.open() || !reader.beginTextSelection())return 7;
    const auto selected=reader.selectedText();
    if(selected.empty())return 8;
    if(!reader.stepSelection(1))return 9;
    if(!reader.saveSelectionHighlight())return 10;
    const auto recorded=books::loadHighlights(argv[2]);
    if(recorded.empty() || recorded.front().begin>=recorded.front().end)
      return 11;
    reader.present(); // highlights must draw over actual Section page pixels
    if(!reader.eraseSelectionHighlight())return 12;
    if(!books::loadHighlights(argv[2]).empty())return 13;
    std::printf("Books unicode selection and persistent highlight: PASS (%s)\n",
                selected.c_str());
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--define")==0) {
    // DI-01: check installed dictionaries from a shell (no framebuffer).
    for(const auto& folder:books::stardictFolders())
      for(const auto& ifo:books::stardict::findDictionaries(folder))
        std::printf("dictionary: %s\n",ifo.c_str());
    const auto matches=books::lookupDefinitions(argv[2]);
    for(const auto& m:matches)
      std::printf("[%s] %s: %s\n",m.source.c_str(),m.headword.c_str(),m.text.c_str());
    return matches.empty()?1:0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-progress")==0) {
    // RD-01/02/03/06 regression: go-to-percent lands near the target and the
    // book percentage only grows; focus reading and auto turn are settings.
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    int last=-1;
    for(int target:{0,10,25,50,75,90,100}) {
      if(!reader.goToBookPercent(target))return 4;
      const int got=reader.bookPercent();
      std::printf("Books go to %d%%: chapter %d page %zu -> %d%%\n",
                  target,reader.currentChapter(),reader.currentPageIndex()+1,got);
      if(got<last)return 5;
      if(target<100 && std::abs(got-target)>6)return 6;
      if(target==100 && (!reader.atBookEnd() || got!=100))return 7;
      last=got;
    }
    if(!reader.goToBookPercent(50))return 4;
    const int before=reader.bookPercent();
    const bool focusWas=reader.preferences().focusReading;
    if(!reader.adjustPreference(16) || reader.preferences().focusReading==focusWas)return 8;
    if(std::abs(reader.bookPercent()-before)>3)return 9;  // re-layout keeps the place
    if(!reader.adjustPreference(16) || reader.preferences().focusReading!=focusWas)return 8;
    const int turnWas=reader.preferences().autoTurnSeconds;
    for(size_t i=0;i<std::size(books::autoTurnChoices);++i)
      if(!reader.adjustPreference(17))return 10;
    if(reader.preferences().autoTurnSeconds!=turnWas)return 10;  // cycles back
    std::puts("Books go-to-percent, progress, focus and auto turn: PASS");
    return 0;
  }
  if(argc==3 && std::strcmp(argv[1],"--test-page-links")==0) {
    Reader reader(argv[2]);
    if(!reader.open())return 3;
    const auto links=reader.pageLinks();
    std::printf("Books Page links: %zu\n",links.size());
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
    std::puts("Books page link jump and return: PASS");
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
    std::printf("Books rendered images: %zu\n",reader.imageCount());
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
  bool dry=false,render=false,walk=false,renderWalk=false,benchmark=false,smooth=false,smoothCheck=false;
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
  } else if(argc==3 && std::strcmp(argv[1],"--smooth-check")==0) {
    dry=true;smoothCheck=true;path=argv[2];
  } else if(argc==4 && std::strcmp(argv[1],"--render-smooth")==0) {
    dry=true;render=true;smooth=true;path=argv[2];output=argv[3];
  } else if (argc==2 && argv[1][0] != '-') {
    path=argv[1];
  } else { usage();return 2; }
  if (!books::isBookFilename(path)) {
    std::fputs("Books: open an .epub or .txt book\n",stderr);
    return 2;
  }
  if(!dry && onCard)(void)books::migrateLegacyState();
  if(!dry && !installExitHandlers())return 5;
  // Destroy the reader and flush progress before releasing the input socket.
  books::Q2SocketInput input;
  const auto openStart=std::chrono::steady_clock::now();
  Reader reader(std::move(path));
  const bool opened=reader.open(!dry);
  if(interrupted)return 0;
  if(!opened) {
    std::fputs("Books: EPUB not supported by current reader slice\n",stderr);
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
        "Books benchmark: open_ms=%lld first_render_us=%lld "
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
            std::fprintf(stderr,"Books: blank rendered page after %zu forward turns\n",
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
      std::printf("Books rendered %zu pages forward and %zu backward without blank text\n",
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
      std::printf("Books walked %zu pages forward and %zu backward\n",
          forward,backward);
    }
    reader.report();
    if (render && !reader.savePgm(output,smooth)) return 4;
    if (smoothCheck) {
      size_t pixels=0;double dx=0,dy=0;
      const double iou=reader.smoothGlyphAgreement(&pixels,&dx,&dy);
      if(iou<0){std::puts("Books smooth glyph agreement: no TrueType glyphs on the page");return 6;}
      std::printf("Books smooth glyph agreement: IoU %.3f over %zu ink pixels, centre shift %+.2f %+.2f px\n",
                  iou,pixels,dx,dy);
    }
    return 0;
  }
  if(!input.open()) {
    std::fputs("Books: input socket unavailable, refusing interactive mode\n",stderr);
    return 5;
  }
  reader.present();
  bool typeMenu=false;
  int typeSelection=0;
  // The Books handoff reader has no shelf, so it offers the settings rows and
  // the reading actions that need none: not bookmarks, links, selection or sync.
  constexpr int kHandoffItems[]={0,1,2,3,4,5,6,7,16,17,18,21,19};
  bool aboutShown=false;
  constexpr int kHandoffCount=static_cast<int>(std::size(kHandoffItems));
  int handoffIndex=0;
  bool percentDialog=false;
  int percentValue=0;
  auto nextAutoTurn=ReaderClock::time_point::max();
  auto nextStatusCheck=ReaderClock::now();
  books::Q2Controls controls(loadWheelSensitivity());
  // A wheel datagram can carry several ticks (MappedEvent::repeat): play each as
  // its own step, before reading the next datagram.
  books::LogicalAction pendingAction=books::LogicalAction::None;
  int pendingRepeat=0;
  while(!interrupted) {
    reader.flushPosition(false);
    books::PhysicalEvent event{};
    if (pendingRepeat==0 && !input.receive(event)) {
      if(!typeMenu && !percentDialog && !aboutShown && ReaderClock::now()>=nextStatusCheck) {
        nextStatusCheck=ReaderClock::now()+std::chrono::seconds(1);
        if(reader.statusChanged())reader.present();
      }
      const auto beforeTurn=nextAutoTurn;
      const auto turnDue=autoTurnTick(reader,!typeMenu && !percentDialog,nextAutoTurn);
      if(beforeTurn!=ReaderClock::time_point::max() && nextAutoTurn!=beforeTurn &&
         beforeTurn<=ReaderClock::now())continue;
      if(waitForReaderInput(input,std::min(reader.nextPositionFlush(),turnDue))<0) {
        std::fputs("Books: input socket poll failed\n",stderr);
        return 5;
      }
      continue;
    }
    if(interrupted)break;
    books::LogicalAction action;
    if(pendingRepeat>0) {
      --pendingRepeat;
      event=books::PhysicalEvent{};
      event.kind=books::PhysicalKind::WheelStep;
      action=pendingAction;
    } else {
      const auto mapped=controls.translate(event);
      action=mapped.action;
      if(mapped.repeat>1){pendingAction=mapped.action;pendingRepeat=mapped.repeat-1;}
    }
    bool changed=false;
    nextAutoTurn=ReaderClock::time_point::max();  // any input restarts the interval
    if(percentDialog) {
      const auto result=handlePercentInput(reader,action,event,percentValue);
      if(result==PercentResult::Open)continue;
      percentDialog=false;
      if(result==PercentResult::Moved){typeMenu=false;reader.present();}
      else reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
      continue;
    }
    if(typeMenu && aboutShown) {
      if(action==books::LogicalAction::Back || action==books::LogicalAction::Confirm ||
         action==books::LogicalAction::Next || action==books::LogicalAction::Previous ||
         event.kind==books::PhysicalKind::TouchUp) {
        aboutShown=false;reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
      }
      continue;
    }
    if(typeMenu) {
      if(action==books::LogicalAction::Back) {
        typeMenu=false;
        reader.present();
      } else if(action==books::LogicalAction::Previous ||
                action==books::LogicalAction::Next) {
        handoffIndex=(handoffIndex+
            (action==books::LogicalAction::Next?1:kHandoffCount-1))%kHandoffCount;
        typeSelection=kHandoffItems[handoffIndex];
        reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
      } else if(action==books::LogicalAction::Confirm ||
                event.kind==books::PhysicalKind::TouchUp) {
        if(event.kind==books::PhysicalKind::TouchUp) {
          // Map the touched row back to a menu item; rows this reader does
          // not offer are ignored.
          const int tapped=books::MenuLayout::itemAt(
              Reader::menuFirstRow(handoffIndex,kHandoffCount),kHandoffCount,event.y);
          if(tapped<0)continue;
          handoffIndex=tapped;
          typeSelection=kHandoffItems[handoffIndex];
        }
        if(typeSelection==19) {
          aboutShown=true;
          reader.showAbout();
          continue;
        }
        if(typeSelection==21) {
          (void)reader.exportNotes();
          reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
          continue;
        }
        if(typeSelection==18) {
          percentDialog=true;
          percentValue=reader.bookPercent();
          reader.showPercentDialog(percentValue);
          continue;
        }
        if(typeSelection>=2)(void)reader.adjustPreference(typeSelection);
        else {
          auto next=reader.typography();
          if(typeSelection==0) {
            next.family=books::nextInstalledTypographyFamily(next);
            next.size=books::nearestTypographySize(next);
          } else next.size=books::nextInstalledTypographySize(next);
          (void)reader.configureTypography(next);
        }
        reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
      }
      continue;
    }
    if(action==books::LogicalAction::Confirm ||
       (event.kind==books::PhysicalKind::TouchUp && event.y<45)) {
      typeMenu=true;
      reader.showTypographyMenu(typeSelection,kHandoffItems,kHandoffCount);
      continue;
    }
    if(action==books::LogicalAction::Back)break;
    if(action==books::LogicalAction::Next)changed=reader.move(1);
    if(action==books::LogicalAction::Previous)changed=reader.move(-1);
    if(event.kind==books::PhysicalKind::TouchUp) {
      changed=reader.move(event.x>=187?1:-1);
    }
    if(changed)reader.present();
  }
  return 0;
}
