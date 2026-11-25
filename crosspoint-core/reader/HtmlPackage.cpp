// SPDX-License-Identifier: MIT
#include "HtmlPackage.hpp"

#include "TxtPackage.hpp"
#include <Utf8.h>
#include <htmlEntities.h>
#include <array>
#include <cerrno>
#include <charconv>
#include <fcntl.h>
#include <map>

namespace books {
namespace {
constexpr size_t sourceLimit=1024*1024, chapterLimit=256*1024, markupLimit=4*1024*1024;
constexpr size_t imageLimit=512*1024, imageBudget=2*1024*1024, maxImages=32;

struct File {
  int fd;
  explicit File(int value):fd(value) {}
  ~File() {if(fd>=0)::close(fd);}
  File(const File&)=delete;
  File& operator=(const File&)=delete;
};

bool readFile(int fd,size_t limit,std::string& data) {
  struct stat st{};
  if(::fstat(fd,&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<0 ||
     static_cast<uint64_t>(st.st_size)>limit)return false;
  data.resize(static_cast<size_t>(st.st_size));
  for(size_t offset=0;offset<data.size();) {
    const auto n=::read(fd,data.data()+offset,data.size()-offset);
    if(n<0 && errno==EINTR)continue;
    if(n<=0)return false;
    offset+=static_cast<size_t>(n);
  }
  char extra;
  ssize_t read;
  do {read=::read(fd,&extra,1);} while(read<0 && errno==EINTR);
  return read==0;
}

bool space(char c) {return c==' ' || c=='\t' || c=='\r' || c=='\n' || c=='\f';}
std::string_view trim(std::string_view text) {
  while(!text.empty() && space(text.front()))text.remove_prefix(1);
  while(!text.empty() && space(text.back()))text.remove_suffix(1);
  return text;
}
char lower(char c) {return c>='A' && c<='Z'?static_cast<char>(c+('a'-'A')):c;}
std::string lowercase(std::string_view text) {
  std::string out(text);
  std::transform(out.begin(),out.end(),out.begin(),lower);
  return out;
}
bool nameChar(char c) {
  c=lower(c);
  return (c>='a' && c<='z') || (c>='0' && c<='9') || c=='_' || c=='-' || c==':';
}
template<class... T>
bool oneOf(std::string_view value,T... names) {return ((value==names)||...);}

constexpr std::array<uint16_t,32> windowsLatin={
  0x20ac,0xfffd,0x201a,0x0192,0x201e,0x2026,0x2020,0x2021,
  0x02c6,0x2030,0x0160,0x2039,0x0152,0xfffd,0x017d,0xfffd,
  0xfffd,0x2018,0x2019,0x201c,0x201d,0x2022,0x2013,0x2014,
  0x02dc,0x2122,0x0161,0x203a,0x0153,0xfffd,0x017e,0x0178};
void appendCodepoint(std::string& out,unsigned cp) {
  if(cp>=0x80 && cp<=0x9f)cp=windowsLatin[cp-0x80];
  if((cp<0x20 && cp!=9 && cp!=10 && cp!=13) || (cp>=0xd800 && cp<=0xdfff) ||
     cp==0xfffe || cp==0xffff || cp>0x10ffff)cp=0xfffd;
  utf8AppendCodepoint(cp,out);
}

std::string cleanDocument(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  if(text.starts_with("\xff\xfe") || text.starts_with("\xfe\xff")) {
    const bool le=static_cast<unsigned char>(text.front())==0xff;
    const auto unit=[&](size_t i) {
      const auto a=static_cast<unsigned char>(text[i]),b=static_cast<unsigned char>(text[i+1]);
      return unsigned(le?(b<<8)|a:(a<<8)|b);
    };
    for(size_t i=2;i<text.size();i+=2) {
      unsigned cp=i+1<text.size()?unit(i):0xfffd;
      if(cp>=0xd800 && cp<=0xdbff && i+3<text.size()) {
        const unsigned low=unit(i+2);
        if(low>=0xdc00 && low<=0xdfff) {cp=0x10000+((cp-0xd800)<<10)+low-0xdc00;i+=2;}
      }
      appendCodepoint(out,cp);
    }
    return out;
  }
  if(text.starts_with("\xef\xbb\xbf"))text.remove_prefix(3);
  for(size_t i=0;i<text.size();) {
    const auto b=static_cast<unsigned char>(text[i]);
    const size_t length=b<0x80?1:b>=0xc2 && b<=0xdf?2:b>=0xe0 && b<=0xef?3:b>=0xf0 && b<=0xf4?4:0;
    const unsigned masks[]={0,0x7f,0x1f,0x0f,0x07},minimum[]={0,0,0x80,0x800,0x10000};
    unsigned cp=b&masks[length];
    size_t used=1;
    while(used<length && i+used<text.size()) {
      const auto next=static_cast<unsigned char>(text[i+used]);
      if((next&0xc0)!=0x80)break;
      cp=(cp<<6)|(next&0x3f);
      ++used;
    }
    if(!length || used!=length || cp<minimum[length] || cp>0x10ffff ||
       (cp>=0xd800 && cp<=0xdfff)) {cp=b;used=1;}
    appendCodepoint(out,cp);
    i+=used;
  }
  return out;
}

std::string entities(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for(size_t i=0;i<text.size();++i) {
    if(text[i]=='&') {
      const auto semicolon=text.substr(i,40).find(';');
      if(semicolon!=text.npos) {
        const auto token=text.substr(i,semicolon+1);
        if(token.starts_with("&#")) {
          auto digits=token.substr(2,token.size()-3);
          const bool hex=!digits.empty() && lower(digits.front())=='x';
          if(hex)digits.remove_prefix(1);
          unsigned cp=0;
          const auto end=digits.data()+digits.size();
          const auto parsed=std::from_chars(digits.data(),end,cp,hex?16:10);
          if(parsed.ec==std::errc{} && parsed.ptr==end) {appendCodepoint(out,cp);i+=semicolon;continue;}
        } else if(token=="&apos;") {out+='\'';i+=semicolon;continue;}
        else if(token=="&quot;") {out+='"';i+=semicolon;continue;}
        else if(const auto* value=lookupHtmlEntity(token.data(),token.size())) {
          out+=value;i+=semicolon;continue;
        }
      }
    }
    out+=text[i];
  }
  return out;
}

struct Tag {
  std::string name;
  bool close=false;
  std::map<std::string,std::string> attributes;

  std::string_view get(const char* key) const {
    const auto it=attributes.find(key);
    return it==attributes.end()?std::string_view{}:it->second;
  }
};

Tag parseTag(std::string_view source) {
  Tag tag;
  tag.close=source.starts_with('/');
  if(tag.close)source.remove_prefix(1);
  if(source.empty() || lower(source.front())<'a' || lower(source.front())>'z')return tag;
  size_t length=0;
  while(length<source.size() && nameChar(source[length]))++length;
  if(!length || length>48)return tag;
  tag.name=lowercase(source.substr(0,length));
  source.remove_prefix(length);
  while(!tag.close && !source.empty() && tag.attributes.size()<32) {
    source=trim(source);
    length=0;
    while(length<source.size() && nameChar(source[length]))++length;
    if(!length) {
      if(!source.empty())source.remove_prefix(1);
      continue;
    }
    const auto key=lowercase(source.substr(0,length));
    source=trim(source.substr(length));
    std::string_view value;
    if(source.starts_with('=')) {
      source=trim(source.substr(1));
      if(source.starts_with('"') || source.starts_with('\'')) {
        const char quote=source.front();
        source.remove_prefix(1);
        const auto end=source.find(quote);
        value=source.substr(0,end);
        source=end==source.npos?std::string_view{}:source.substr(end+1);
      } else {
        size_t end=0;
        while(end<source.size() && !space(source[end]))++end;
        value=source.substr(0,end);
        source.remove_prefix(end);
      }
    }
    if(key.size()<=32 && value.size()<=2048)tag.attributes.emplace(key,entities(value));
  }
  return tag;
}

size_t tagEnd(std::string_view input,size_t start) {
  char quote=0;
  for(size_t i=start;i<input.size() && i-start<8192;++i) {
    const char c=input[i];
    if(quote) {if(c==quote)quote=0;}
    else if(c=='>')return i;
    else if(c=='<')break;
    else if(c=='"' || c=='\'')quote=c;
  }
  return input.npos;
}

size_t rawClose(std::string_view input,const std::string& tag,size_t start) {
  for(auto pos=input.find("</",start);pos!=input.npos;pos=input.find("</",pos+2)) {
    const size_t next=pos+2+tag.size();
    if(next<input.size() && (space(input[next]) || input[next]=='>') &&
       std::equal(tag.begin(),tag.end(),input.begin()+pos+2,
         [](char a,char b){return a==lower(b);}))return pos;
  }
  return input.size();
}

std::string styleDeclarations(std::string_view text) {
  std::string out;
  while(!text.empty() && out.size()<1024) {
    const auto end=text.find(';');
    const auto part=text.substr(0,end);
    text=end==text.npos?std::string_view{}:text.substr(end+1);
    const auto delim=part.find(':');
    if(delim==part.npos)continue;
    const auto name=lowercase(trim(part.substr(0,delim)));
    const auto value=trim(part.substr(delim+1));
    if(!oneOf(name,"text-align","font-weight","font-style","font-size","text-decoration",
              "text-indent","line-height","margin","margin-top","margin-bottom","margin-left",
              "margin-right","padding","padding-top","padding-bottom","padding-left",
              "padding-right","white-space","display","direction","list-style-type",
              "page-break-before","page-break-after","break-before","break-after"))continue;
    if(value.empty() || value.size()>64 || !std::all_of(value.begin(),value.end(),[](char c){
      return nameChar(c) || space(c) || c=='.' || c=='%' || c=='!' || c==','; }))continue;
    out+=name+":"+std::string(value)+";";
  }
  return out;
}

void appendStylesheet(std::string_view text,std::string& output) {
  if(text.size()>32*1024)return;
  while(!text.empty() && output.size()<32*1024) {
    const auto a=text.find('{'),b=text.find('}');
    if(a==text.npos || b==text.npos)break;
    if(a>b) {text.remove_prefix(b+1);continue;}
    auto selector=trim(text.substr(0,a));
    const auto directive=selector.find_last_of(';');
    if(directive!=selector.npos)selector=trim(selector.substr(directive+1));
    const auto style=styleDeclarations(text.substr(a+1,b-a-1));
    if(!style.empty() && !selector.empty() && selector.size()<=160 &&
       std::all_of(selector.begin(),selector.end(),[](char c){
         return nameChar(c) || space(c) || c=='.' || c=='#' || c==',' || c=='>' || c=='*';}))
      output+=std::string(selector)+"{"+style+"}\n";
    text.remove_prefix(b+1);
  }
}

std::string decodePath(std::string_view text) {
  std::string out;
  for(size_t i=0;i<text.size();++i) {
    unsigned ch=static_cast<unsigned char>(text[i]);
    if(ch=='%') {
      if(i+2>=text.size())return {};
      const auto end=text.data()+i+3;
      const auto parsed=std::from_chars(text.data()+i+1,end,ch,16);
      if(parsed.ec!=std::errc{} || parsed.ptr!=end)return {};
      i+=2;
    }
    if(ch<32 || ch==127)return {};
    out+=static_cast<char>(ch);
  }
  return out;
}

bool localImage(int root,std::string_view path,std::string& data) {
  if(path.empty() || path.size()>512 || path.find_first_of(":\\?#")!=path.npos)return false;
  File directory(::dup(root));
  for(unsigned depth=0;directory.fd>=0 && depth<8;++depth) {
    const auto slash=path.find('/');
    const std::string part(path.substr(0,slash));
    if(part=="." && slash!=path.npos) {
      path.remove_prefix(slash+1);
      continue;
    }
    if(part.empty() || part.front()=='.')return false;
    if(slash==path.npos) {
      File image(::openat(directory.fd,part.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
      return readFile(image.fd,imageLimit,data);
    }
    const int next=::openat(directory.fd,part.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);
    ::close(directory.fd);
    directory.fd=next;
    path.remove_prefix(slash+1);
  }
  return false;
}

std::string chapterFile(size_t index) {return "h"+std::to_string(index)+".xhtml";}
bool heading(std::string_view tag) {return tag.size()==2 && tag[0]=='h' && tag[1]>='1' && tag[1]<='6';}
bool block(std::string_view tag) {
  return heading(tag) || oneOf(tag,"p","div","section","article","main","aside","header","footer",
      "nav","blockquote","pre","ul","ol","li","dl","dt","dd","table","thead","tbody","tfoot","tr",
      "td","th","caption","figure","figcaption","hr");
}
bool inlineTag(std::string_view tag) {
  return oneOf(tag,"a","span","strong","b","em","i","u","s","del","strike","small","code","kbd","samp",
      "sub","sup","ruby","rt","rp","br","img");
}

struct Html {
  struct Link {size_t offset;std::string target;};
  struct Chapter {std::string body;std::vector<Link> links;};
  struct Anchor {size_t chapter;std::string id;};
  struct Heading {Anchor anchor;std::string label;};
  struct Image {std::string name,data;bool png;};
  const int root;
  explicit Html(int folder):root(folder) {}
  std::vector<Chapter> chapters{1};
  struct OpenTag {std::string name,continuation;};
  std::vector<OpenTag> stack;
  std::map<std::string,Anchor> anchors;
  std::vector<Heading> headings;
  std::map<std::string,std::string> imagePaths;
  std::vector<Image> images;
  size_t total=0,imageBytes=0,idCount=0,linkCount=0;
  std::string title,language,css,bodyStyle,bodyClass,bodyDirection;
  bool overflow=false,head=false;
  std::vector<std::string> hidden;
  size_t pendingHeading=0;

  void append(std::string_view text) {
    auto& body=chapters.back().body;
    if(text.size()>chapterLimit-body.size() || text.size()>markupLimit-total) {overflow=true;return;}
    body+=text;
    total+=text.size();
  }
  void text(std::string_view raw) {
    if(head || !hidden.empty())return;
    const auto plain=entities(raw);
    append(txtpkg::xmlEscape(plain));
    if(pendingHeading) {
      auto& label=headings[pendingHeading-1].label;
      if(label.size()+plain.size()<=240)label+=plain;
    }
  }
  void closeTo(size_t depth) {
    while(stack.size()>depth) {
      if(heading(stack.back().name))pendingHeading=0;
      append("</"+stack.back().name+">");
      stack.pop_back();
    }
  }
  void close(std::string_view tag,bool stopAtContainer=false) {
    for(size_t i=stack.size();i>0;--i) {
      if(stack[i-1].name==tag) {closeTo(i-1);return;}
      if(stopAtContainer && oneOf(stack[i-1].name,"ul","ol","dl","table","tr"))return;
    }
  }
  void startChapterIfNeeded(std::string_view next) {
    if(!block(next) || chapters.size()>=512)return;
    const auto size=chapters.back().body.size();
    if(size<32768 && !(heading(next) && size>=1024))return;
    // Close and reopen common page wrappers. Lists, tables and preformatted
    // blocks stay intact, and IDs are only emitted in the original chapter.
    if(!std::all_of(stack.begin(),stack.end(),[](const OpenTag& tag){
      return oneOf(tag.name,"div","main","section","article");
    }))return;
    auto wrappers=stack;
    closeTo(0);
    chapters.emplace_back();
    for(const auto& tag:wrappers)append(tag.continuation);
    stack=std::move(wrappers);
  }
  Anchor newAnchor() {return {chapters.size()-1,"a"+std::to_string(++idCount)};}
  static std::string attr(std::string_view name,std::string_view value) {
    return value.empty()?std::string{}:" "+std::string(name)+"=\""+txtpkg::xmlEscape(value)+"\"";
  }
  std::string image(std::string_view source) {
    const auto path=decodePath(source);
    if(path.empty())return {};
    if(const auto it=imagePaths.find(path);it!=imagePaths.end())return it->second;
    if(images.size()>=maxImages || imagePaths.size()>=maxImages*2)return {};
    auto& result=imagePaths[path];
    const bool png=hasBookSuffix(path,".png");
    if(!png && !hasBookSuffix(path,".jpg") && !hasBookSuffix(path,".jpeg"))return {};
    std::string bytes;
    if(!localImage(root,path,bytes) || bytes.size()>imageBudget-imageBytes ||
       (png?!bytes.starts_with("\x89PNG\r\n\x1a\n"):!bytes.starts_with("\xff\xd8\xff")))return {};
    imageBytes+=bytes.size();
    result="image"+std::to_string(images.size())+(png?".png":".jpg");
    images.push_back({result,std::move(bytes),png});
    return result;
  }
  void element(const Tag& tag) {
    const auto& name=tag.name;
    if(name=="head") {head=!tag.close;return;}
    if(name=="html" || name=="body") {
      if(!tag.close) {
        const auto lang=tag.get("lang").empty()?tag.get("xml:lang"):tag.get("lang");
        if(!lang.empty() && lang.size()<=48)language=lang;
        if(name=="body") {head=false;bodyStyle=styleDeclarations(tag.get("style"));
          bodyClass=tag.get("class");bodyDirection=tag.get("dir");}
      }
      return;
    }
    if(!block(name) && !inlineTag(name))return;
    if(head && tag.close)return;
    head=false;  // a body tag also closes a misplaced or omitted </head>
    if(!hidden.empty()) {
      if(tag.close) {
        const auto found=std::find(hidden.rbegin(),hidden.rend(),name);
        if(found!=hidden.rend())hidden.erase(found.base()-1,hidden.end());
      }
      else if(!tag.close && !oneOf(name,"br","hr","img")) {
        if(hidden.size()>=64)overflow=true;
        else hidden.push_back(name);
      }
      return;
    }
    if(!tag.close && tag.attributes.count("hidden")) {
      if(!oneOf(name,"br","hr","img"))hidden.push_back(name);
      return;
    }
    if(tag.close) {close(name);return;}
    if(block(name))close("p",true);
    if(name=="li")close("li",true);
    if(oneOf(name,"dt","dd")) {close("dt",true);close("dd",true);}
    if(name=="tr") {close("td",true);close("th",true);close("tr",true);}
    if(oneOf(name,"td","th")) {close("td",true);close("th",true);}
    if(name=="a")close("a");
    if(heading(name)) {
      for(size_t i=stack.size();i>0;--i)if(heading(stack[i-1].name)){closeTo(i-1);break;}
    }
    startChapterIfNeeded(name);

    std::string attrs=attr("class",tag.get("class"))+attr("style",styleDeclarations(tag.get("style")));
    if(oneOf(tag.get("dir"),"rtl","ltr","auto"))attrs+=attr("dir",tag.get("dir"));
    const auto lang=tag.get("xml:lang").empty()?tag.get("lang"):tag.get("xml:lang");
    if(lang.size()<=48)attrs+=attr("xml:lang",lang);
    const auto continuation=oneOf(name,"div","main","section","article")?
        "<"+name+attrs+">":std::string{};
    const auto explicitId=tag.get("id").empty() && name=="a"?tag.get("name"):tag.get("id");
    if(idCount<4096 && ((!explicitId.empty() && explicitId.size()<=256) ||
        (heading(name) && headings.size()<512))) {
      const auto anchor=newAnchor();
      if(name=="span")append("<a"+attr("id",anchor.id)+"></a>");
      else attrs+=attr("id",anchor.id);
      if(!explicitId.empty() && explicitId.size()<=256)anchors.try_emplace(std::string(explicitId),anchor);
      if(heading(name) && headings.size()<512) {headings.push_back({anchor,{}});pendingHeading=headings.size();}
    }
    if(oneOf(name,"ol","td","th")) {
      const char* key=name=="ol"?"start":"colspan";
      const auto value=tag.get(key);
      if(!value.empty() && value.size()<=4 && value.find_first_not_of("0123456789")==value.npos)
        attrs+=attr(key,value);
    }
    if(name=="img") {
      const auto file=image(tag.get("src"));
      if(file.empty())append(txtpkg::xmlEscape(tag.get("alt")));
      else append("<img"+attrs+attr("src",file)+attr("alt",tag.get("alt"))+"/>");
      return;
    }
    if(name=="br" || name=="hr") {append("<"+name+attrs+"/>");return;}
    if(stack.size()>=64) {overflow=true;return;}
    const auto href=tag.get("href");
    if(name=="a" && href.starts_with('#') && href.size()>1 && href.size()<=257 && linkCount<4096) {
      const auto target=decodePath(href.substr(1));
      if(!target.empty()) {
        ++linkCount;
        chapters.back().links.push_back({chapters.back().body.size()+2,target});
      }
    }
    append("<"+name+attrs+">");
    stack.push_back({name,continuation});
  }

  bool parse(std::string_view input) {
    size_t at=0;
    while(at<input.size() && !overflow) {
      const auto mark=input.find('<',at);
      text(input.substr(at,mark==input.npos?mark:mark-at));
      if(mark==input.npos)break;
      if(input.substr(mark).starts_with("<!--")) {
        const auto end=input.find("-->",mark+4);
        at=end==input.npos?input.size():end+3;
        continue;
      }
      const auto end=tagEnd(input,mark+1);
      if(end==input.npos) {text("<");at=mark+1;continue;}
      const auto source=input.substr(mark+1,end-mark-1);
      at=end+1;
      if(source.starts_with('!') || source.starts_with('?'))continue;
      const auto tag=parseTag(source);
      if(tag.name.empty()) {text(input.substr(mark,end-mark+1));continue;}
      if(!tag.close && oneOf(tag.name,"script","iframe","object","applet","embed","style","title",
                           "textarea","select","template","svg","math","noscript","video","audio")) {
        if(tag.name=="embed")continue;
        const auto close=rawClose(input,tag.name,at);
        const auto content=input.substr(at,close-at);
        if(tag.name=="title" && title.empty() && content.size()<=512)title=entities(trim(content));
        if(tag.name=="style")appendStylesheet(content,css);
        at=close;
        continue;
      }
      element(tag);
    }
    closeTo(0);
    return !overflow;
  }

  std::string package() {
    txtpkg::ZipWriter zip;
    txtpkg::startEpub(zip);
    std::string manifest,spine,nav;
    size_t navIndex=0,nextHeading=0,linkBytes=0;
    const auto addNav=[&](const std::string& target,std::string_view label) {
      const auto id=std::to_string(++navIndex);
      nav+="<navPoint id=\"n"+id+"\" playOrder=\""+id+"\"><navLabel><text>"+
          txtpkg::xmlEscape(label)+"</text></navLabel><content src=\""+target+"\"/></navPoint>";
    };
    if(!css.empty()) {
      zip.add("OEBPS/book.css",css);
      manifest+="<item id=\"css\" href=\"book.css\" media-type=\"text/css\"/>";
    }
    for(size_t i=0;i<chapters.size();++i) {
      const auto& chapter=chapters[i];
      const auto file=chapterFile(i),id="h"+std::to_string(i);
      std::string xhtml="<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>"+
          txtpkg::xmlEscape(title)+"</title>";
      if(!css.empty())xhtml+="<link rel=\"stylesheet\" type=\"text/css\" href=\"book.css\"/>";
      xhtml+="</head><body"+attr("class",bodyClass)+attr("style",bodyStyle)+attr("dir",bodyDirection)+">";
      size_t offset=0;
      for(const auto& link:chapter.links) {
        const auto found=anchors.find(link.target);
        if(found==anchors.end())continue;
        const auto& to=found->second;
        const auto href=attr("href",chapterFile(to.chapter)+"#"+to.id);
        linkBytes+=href.size();
        if(total+linkBytes>markupLimit)return {};
        xhtml.append(chapter.body,offset,link.offset-offset);
        xhtml+=href;
        offset=link.offset;
      }
      xhtml.append(chapter.body,offset,std::string::npos);
      zip.add("OEBPS/"+file,xhtml+"</body></html>");
      manifest+="<item id=\""+id+"\" href=\""+file+"\" media-type=\"application/xhtml+xml\"/>";
      spine+="<itemref idref=\""+id+"\"/>";
      if(nextHeading==headings.size() || headings[nextHeading].anchor.chapter!=i)
        addNav(file,"Part "+std::to_string(i+1));
      while(nextHeading<headings.size() && headings[nextHeading].anchor.chapter==i) {
        const auto& h=headings[nextHeading++];
        const auto text=trim(h.label);
        if(!text.empty())addNav(file+"#"+h.anchor.id,text);
      }
    }
    for(const auto& image:images) {
      zip.add("OEBPS/"+image.name,image.data);
      manifest+="<item id=\""+image.name+"\" href=\""+image.name+
          "\" media-type=\""+(image.png?"image/png":"image/jpeg")+"\"/>";
    }
    txtpkg::finishEpub(zip,title,"books-html",language.empty()?"und":language,manifest,spine,nav);
    return zip.finish();
  }
};

uint64_t hash(std::string_view bytes) {
  uint64_t value=14695981039346656037ull;
  for(unsigned char byte:bytes) {value^=byte;value*=1099511628211ull;}
  return value;
}
}  // namespace

std::string packageHtmlBook(const std::string& source,const std::string& cacheDir,std::string& error) {
  File file(::open(source.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC));
  std::string raw;
  if(!readFile(file.fd,sourceLimit,raw)) {error="could not read an HTML file up to 1 MiB";return {};}
  const auto slash=source.find_last_of('/');
  const auto base=slash==source.npos?".":slash==0?"/":source.substr(0,slash);
  const auto filename=std::string_view(source).substr(slash==source.npos?0:slash+1);
  File directory(::open(base.c_str(),O_RDONLY|O_DIRECTORY|O_CLOEXEC));
  Html html(directory.fd);
  if(!html.parse(cleanDocument(raw))) {error="HTML exceeded conversion limits";return {};}
  std::string().swap(raw);
  if(html.title.empty())html.title=cleanDocument(filename.substr(0,filename.rfind('.')));
  const auto payload=html.package();
  if(payload.empty()) {error="HTML exceeded package limits";return {};}
  // The bundle identity includes the accepted local image bytes.
  const auto path=cacheDir+"/html-v1-"+std::to_string(hash(source))+"-"+std::to_string(hash(payload))+".epub";
  struct stat cached{};
  if(::lstat(path.c_str(),&cached)==0 && S_ISREG(cached.st_mode) &&
     static_cast<uint64_t>(cached.st_size)==payload.size())return path;
  ::mkdir(cacheDir.c_str(),0755);
  std::string temp=path+".partXXXXXX";
  const int fd=::mkstemp(temp.data());
  if(fd>=0) {
    if(FILE* out=::fdopen(fd,"wb")) {
      const bool wrote=std::fwrite(payload.data(),1,payload.size(),out)==payload.size();
      if(std::fclose(out)==0 && wrote && ::rename(temp.c_str(),path.c_str())==0)return path;
    } else ::close(fd);
    ::unlink(temp.c_str());
  }
  error="could not write the HTML cache";
  return {};
}
}  // namespace books
