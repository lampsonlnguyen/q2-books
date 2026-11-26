// SPDX-License-Identifier: MIT
#pragma once
#include "MobiArtwork.hpp"
#include <charconv>
#include <map>
#include <set>
#include <cstdlib>
#include <utility>

namespace books::mobipkg {
constexpr size_t chapterBytes=48*1024, markupBytes=24u*1024*1024, maxChapters=2048;
constexpr size_t noPosition=size_t(-1);
inline bool space(char c){return c==' '||c=='\t'||c=='\r'||c=='\n'||c=='\f';}
inline char lower(char c){return c>='A'&&c<='Z'?char(c+32):c;}
inline bool nameChar(char c){c=lower(c);return (c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-'||c=='_'||c==':';}
inline std::string lowerName(std::string_view s){std::string out(s);std::transform(out.begin(),out.end(),out.begin(),lower);return out;}
inline size_t number(std::string_view s){size_t n=0;if(s.empty()||s.size()>12)return noPosition;const auto r=std::from_chars(s.data(),s.data()+s.size(),n);return r.ec==std::errc{}&&r.ptr==s.data()+s.size()?n:noPosition;}
inline unsigned entity(std::string_view s){
  if(s.starts_with('#')){s.remove_prefix(1);int base=10;if(!s.empty()&&lower(s.front())=='x'){s.remove_prefix(1);base=16;}unsigned n=0;const auto r=std::from_chars(s.data(),s.data()+s.size(),n,base);return !s.empty()&&r.ec==std::errc{}&&r.ptr==s.data()+s.size()&&xmlChar(n)?n:0;}
  static constexpr std::pair<std::string_view,unsigned> names[]={
    {"AElig",198},{"Aacute",193},{"Acirc",194},{"Agrave",192},{"Alpha",913},{"Aring",197},{"Atilde",195},
    {"Auml",196},{"Beta",914},{"Ccedil",199},{"Chi",935},{"Dagger",8225},{"Delta",916},{"ETH",208},
    {"Eacute",201},{"Ecirc",202},{"Egrave",200},{"Epsilon",917},{"Eta",919},{"Euml",203},{"Gamma",915},
    {"Iacute",205},{"Icirc",206},{"Igrave",204},{"Iota",921},{"Iuml",207},{"Kappa",922},{"Lambda",923},
    {"Mu",924},{"Ntilde",209},{"Nu",925},{"OElig",338},{"Oacute",211},{"Ocirc",212},{"Ograve",210},
    {"Omega",937},{"Omicron",927},{"Oslash",216},{"Otilde",213},{"Ouml",214},{"Phi",934},{"Pi",928},
    {"Prime",8243},{"Psi",936},{"Rho",929},{"Scaron",352},{"Sigma",931},{"THORN",222},{"Tau",932},{"Theta",920},
    {"Uacute",218},{"Ucirc",219},{"Ugrave",217},{"Upsilon",933},{"Uuml",220},{"Xi",926},{"Yacute",221},
    {"Yuml",376},{"Zeta",918},{"aacute",225},{"acirc",226},{"acute",180},{"aelig",230},{"agrave",224},
    {"alefsym",8501},{"alpha",945},{"amp",38},{"and",8743},{"ang",8736},{"apos",39},{"aring",229},
    {"asymp",8776},{"atilde",227},{"auml",228},{"bdquo",8222},{"beta",946},{"brvbar",166},{"bull",8226},
    {"cap",8745},{"ccedil",231},{"cedil",184},{"cent",162},{"chi",967},{"circ",710},{"clubs",9827},
    {"cong",8773},{"copy",169},{"crarr",8629},{"cup",8746},{"curren",164},{"dArr",8659},{"dagger",8224},
    {"darr",8595},{"deg",176},{"delta",948},{"diams",9830},{"divide",247},{"eacute",233},{"ecirc",234},
    {"egrave",232},{"empty",8709},{"emsp",8195},{"ensp",8194},{"epsilon",949},{"equiv",8801},{"eta",951},
    {"eth",240},{"euml",235},{"euro",8364},{"exist",8707},{"fnof",402},{"forall",8704},{"frac12",189},
    {"frac14",188},{"frac34",190},{"frasl",8260},{"gamma",947},{"ge",8805},{"gt",62},{"hArr",8660},
    {"harr",8596},{"hearts",9829},{"hellip",8230},{"iacute",237},{"icirc",238},{"iexcl",161},{"igrave",236},
    {"image",8465},{"infin",8734},{"int",8747},{"iota",953},{"iquest",191},{"isin",8712},{"iuml",239},
    {"kappa",954},{"lArr",8656},{"lambda",955},{"lang",9001},{"laquo",171},{"larr",8592},{"lceil",8968},
    {"ldquo",8220},{"le",8804},{"lfloor",8970},{"lowast",8727},{"loz",9674},{"lrm",8206},{"lsaquo",8249},
    {"lsquo",8216},{"lt",60},{"macr",175},{"mdash",8212},{"micro",181},{"middot",183},{"minus",8722},{"mu",956},
    {"nabla",8711},{"nbsp",160},{"ndash",8211},{"ne",8800},{"ni",8715},{"not",172},{"notin",8713},{"nsub",8836},
    {"ntilde",241},{"nu",957},{"oacute",243},{"ocirc",244},{"oelig",339},{"ograve",242},{"oline",8254},
    {"omega",969},{"omicron",959},{"oplus",8853},{"or",8744},{"ordf",170},{"ordm",186},{"oslash",248},
    {"otilde",245},{"otimes",8855},{"ouml",246},{"para",182},{"part",8706},{"permil",8240},{"perp",8869},
    {"phi",966},{"pi",960},{"piv",982},{"plusmn",177},{"pound",163},{"prime",8242},{"prod",8719},{"prop",8733},
    {"psi",968},{"quot",34},{"rArr",8658},{"radic",8730},{"rang",9002},{"raquo",187},{"rarr",8594},
    {"rceil",8969},{"rdquo",8221},{"real",8476},{"reg",174},{"rfloor",8971},{"rho",961},{"rlm",8207},
    {"rsaquo",8250},{"rsquo",8217},{"sbquo",8218},{"scaron",353},{"sdot",8901},{"sect",167},{"shy",173},
    {"sigma",963},{"sigmaf",962},{"sim",8764},{"spades",9824},{"sub",8834},{"sube",8838},{"sum",8721},
    {"sup",8835},{"sup1",185},{"sup2",178},{"sup3",179},{"supe",8839},{"szlig",223},{"tau",964},{"there4",8756},
    {"theta",952},{"thetasym",977},{"thinsp",8201},{"thorn",254},{"tilde",732},{"times",215},{"trade",8482},
    {"uArr",8657},{"uacute",250},{"uarr",8593},{"ucirc",251},{"ugrave",249},{"uml",168},{"upsih",978},
    {"upsilon",965},{"uuml",252},{"weierp",8472},{"xi",958},{"yacute",253},{"yen",165},{"yuml",255},
    {"zeta",950},{"zwj",8205},{"zwnj",8204},
  };
  const auto it=std::lower_bound(std::begin(names),std::end(names),s,[](const auto& item,std::string_view name){return item.first<name;});
  return it==std::end(names)||it->first!=s?0:it->second;
}
inline void xmlCode(std::string& out,unsigned c){switch(c){case '&':out+="&amp;";break;case '<':out+="&lt;";break;case '>':out+="&gt;";break;case '"':out+="&quot;";break;default:utf8(out,c);break;}}
inline std::string escape(std::string_view s,unsigned encoding=65001,bool html=false){
  std::string out;out.reserve(s.size());bool valid=true;
  for(size_t i=0;i<s.size();){
    if(html&&s[i]=='&'){const auto semi=s.substr(i,std::min<size_t>(32,s.size()-i)).find(';');if(semi!=s.npos&&semi>1){const unsigned c=entity(s.substr(i+1,semi-1));if(c){xmlCode(out,c);i+=semi+1;continue;}}}
    xmlCode(out,codepoint(s,i,encoding==1252,valid));
  }
  return out;
}
struct Tag {
  std::string name;bool closing=false;std::map<std::string,std::string_view> attrs;
  std::string_view get(const char* key) const {const auto it=attrs.find(key);return it==attrs.end()?std::string_view{}:it->second;}
};
inline Tag parseTag(std::string_view s){
  Tag t;if(s.starts_with('/')){t.closing=true;s.remove_prefix(1);}while(!s.empty()&&space(s.front()))s.remove_prefix(1);
  size_t n=0;while(n<s.size()&&nameChar(s[n]))++n;if(!n||n>48)return t;t.name=lowerName(s.substr(0,n));s.remove_prefix(n);
  while(!t.closing&&!s.empty()&&t.attrs.size()<32){
    while(!s.empty()&&space(s.front()))s.remove_prefix(1);
    n=0;while(n<s.size()&&nameChar(s[n]))++n;if(!n){if(!s.empty())s.remove_prefix(1);continue;}
    const auto key=lowerName(s.substr(0,n));s.remove_prefix(n);while(!s.empty()&&space(s.front()))s.remove_prefix(1);
    if(s.empty()||s.front()!='=')continue;
    s.remove_prefix(1);while(!s.empty()&&space(s.front()))s.remove_prefix(1);if(s.empty())break;
    std::string_view value;
    if(s.front()=='"'||s.front()=='\''){const char q=s.front();s.remove_prefix(1);const auto end=s.find(q);value=s.substr(0,end);s=end==s.npos?std::string_view{}:s.substr(end+1);}
    else {size_t end=0;while(end<s.size()&&!space(s[end]))++end;value=s.substr(0,end);s.remove_prefix(end);if(s.empty()&&value.ends_with('/'))value.remove_suffix(1);}
    if(key.size()<=32&&value.size()<=2048)t.attrs.try_emplace(key,value);
  }
  return t;
}
// Recover malformed legacy attributes at the first '>' rather than swallowing
// a following paragraph. Bound each scan to avoid quadratic work on bad input.
inline size_t tagEnd(std::string_view s,size_t at){
  char quote=0;size_t fallback=s.npos;
  for(size_t i=at;i<s.size()&&i-at<8192;++i){const char c=s[i];if(c=='<')break;if(c=='>'&&fallback==s.npos)fallback=i;
    if(quote){if(c==quote)quote=0;}else if(c=='>')return i;else if(c=='"'||c=='\'')quote=c;}
  return fallback;
}
template<class F> inline void scan(std::string_view s,F&& visit){
  for(size_t at=0;at<s.size();){
    const auto mark=s.find('<',at);if(mark==s.npos){visit(at,s.size(),s.substr(at),nullptr);break;}
    if(mark>at)visit(at,mark,s.substr(at,mark-at),nullptr);
    if(s.substr(mark).starts_with("<!--")){const auto end=s.find("-->",mark+4);at=end==s.npos?s.size():end+3;continue;}
    const auto end=tagEnd(s,mark+1);if(end==s.npos){visit(mark,mark+1,s.substr(mark,1),nullptr);at=mark+1;continue;}
    const auto raw=s.substr(mark+1,end-mark-1);at=end+1;
    if(raw.starts_with('!')||raw.starts_with('?'))continue;
    const Tag tag=parseTag(raw);if(tag.name.empty())visit(mark,at,s.substr(mark,at-mark),nullptr);else visit(mark,at,{},&tag);
  }
}
inline void le16(std::string& s,uint32_t v){s+=char(v&255);s+=char((v>>8)&255);}
inline void le32(std::string& s,uint32_t v){le16(s,v&65535);le16(s,v>>16);}
// Stored ZIP: write payloads directly to the SD file rather than holding a
// second copy of the whole ebook in RAM. No additional target dependencies.
struct Zip {
  struct Entry{std::string name;uint32_t crc,size,offset;};
  FILE* f;bool ok=true;uint32_t offset=0;std::vector<Entry> entries;
  explicit Zip(FILE* file):f(file){}
  void write(std::string_view s){if(!ok)return;if(s.size()>64u*1024*1024-offset||std::fwrite(s.data(),1,s.size(),f)!=s.size()){ok=false;return;}offset+=static_cast<uint32_t>(s.size());}
  void add(const std::string& name,std::string_view body){
    if(!ok||entries.size()>=4096){ok=false;return;}
    const Entry e{name,static_cast<uint32_t>(::crc32(0,reinterpret_cast<const Bytef*>(body.data()),static_cast<uInt>(body.size()))),static_cast<uint32_t>(body.size()),offset};
    std::string h;le32(h,0x04034b50);le16(h,10);le16(h,0);le16(h,0);le16(h,0);le16(h,0x21);le32(h,e.crc);le32(h,e.size);le32(h,e.size);le16(h,name.size());le16(h,0);h+=name;write(h);write(body);entries.push_back(e);
  }
  bool finish(){const uint32_t start=offset;
    for(const auto& e:entries){std::string h;le32(h,0x02014b50);le16(h,20);le16(h,10);le16(h,0);le16(h,0);le16(h,0);le16(h,0x21);le32(h,e.crc);le32(h,e.size);le32(h,e.size);le16(h,e.name.size());le16(h,0);le16(h,0);le16(h,0);le16(h,0);le32(h,0);le32(h,e.offset);h+=e.name;write(h);}
    const uint32_t bytes=offset-start;std::string e;le32(e,0x06054b50);le16(e,0);le16(e,0);le16(e,entries.size());le16(e,entries.size());le32(e,bytes);le32(e,start);le16(e,0);write(e);return ok;
  }
};
inline std::string chapterName(size_t i){return "c"+std::to_string(i)+".xhtml";}
inline bool heading(std::string_view s){return s.size()==2&&s[0]=='h'&&s[1]>='1'&&s[1]<='6';}
inline bool block(std::string_view s){return heading(s)||s=="p"||s=="div"||s=="blockquote"||s=="center"||s=="li"||s=="section"||s=="article"||s=="pre"||s=="tr"||s=="td"||s=="th";}
inline std::string inlineName(std::string_view s){if(s=="strong")return "b";if(s=="em")return "i";if(s=="strike"||s=="del")return "s";if(s=="b"||s=="i"||s=="u"||s=="s"||s=="sup"||s=="sub"||s=="small"||s=="a")return std::string(s);return {};}
struct Import {
  struct Link{size_t at,target;};
  struct Chapter{std::string body,label;std::vector<Link> links;bool visible=false;};
  Book& book;FILE* source;Zip& zip;std::vector<Chapter> chapters{1};
  std::string paragraph,hidden;std::vector<std::string> inlines;
  std::set<size_t> targets;std::set<size_t>::const_iterator next;
  std::map<size_t,size_t> anchors;std::map<size_t,std::string> images;
  size_t total=0,imageTotal=0;bool failed=false;
  Import(Book& b,FILE* f,Zip& z):book(b),source(f),zip(z){}
  Chapter& current(){return chapters.back();}
  void append(std::string_view s){if(failed)return;if(s.size()>markupBytes-total){failed=true;return;}current().body.append(s);total+=s.size();}
  void closeInline(size_t depth){while(inlines.size()>depth){append("</"+inlines.back()+">");inlines.pop_back();}}
  void closeBlock(){closeInline(0);if(!paragraph.empty()){append("</"+paragraph+">");paragraph.clear();}}
  void split(){closeBlock();if(current().visible&&chapters.size()<maxChapters)chapters.emplace_back();else if(current().body.size()>256*1024)failed=true;}
  void startBlock(std::string name="p",std::string_view align={}){closeBlock();if(current().body.size()>=chapterBytes)split();paragraph=std::move(name);std::string attr;if(align=="left"||align=="right"||align=="center"||align=="justify")attr=" style=\"text-align:"+std::string(align)+"\"";append("<"+paragraph+attr+">");}
  void ensureBlock(){if(paragraph.empty())startBlock();}
  void markAnchors(size_t through){while(next!=targets.end()&&*next<=through){const size_t pos=*next++;anchors[pos]=chapters.size()-1;const bool inside=std::find(inlines.begin(),inlines.end(),"a")!=inlines.end();const auto tag=inside?"span":"a";append("<"+std::string(tag)+" id=\"pos"+std::to_string(pos)+"\"></"+tag+">");}}
  void text(std::string_view raw,bool html=true){
    if(raw.empty()||(!hidden.empty()))return;
    const bool visible=!std::all_of(raw.begin(),raw.end(),space);if(!visible&&paragraph.empty())return;
    while(!raw.empty()&&!failed){
      if(current().body.size()>=chapterBytes&&current().visible)split();
      size_t n=std::min<size_t>(8192,raw.size());if(n<raw.size()&&book.encoding==65001){while(n&&((byte(raw,n)&0xc0)==0x80))--n;}
      if(n<raw.size()&&html){const auto amp=raw.substr(n>32?n-32:0,std::min<size_t>(n,32)).rfind('&');if(amp!=raw.npos){const size_t pos=(n>32?n-32:0)+amp;if(raw.substr(pos,n-pos).find(';')==raw.npos&&pos>0)n=pos;}}
      if(!n)n=std::min<size_t>(8192,raw.size());
      ensureBlock();const auto part=raw.substr(0,n);const auto escaped=escape(part,book.encoding,html);append(escaped);current().visible|=!std::all_of(part.begin(),part.end(),space);
      if(heading(paragraph)&&current().label.empty()&&part.size()<240)current().label=metadataText(part,book.encoding,240);
      raw.remove_prefix(n);
    }
  }
  std::string image(size_t id){
    if(!id||id==noPosition)return {};
    const auto found=images.find(id);if(found!=images.end())return found->second;
    if(images.size()>=128)return {};
    auto& name=images[id];std::string data;
    if(!imageBytes(source,book,id,data)||!normalizeArtwork(data)||data.size()>12u*1024*1024-imageTotal)return {};
    name="img"+std::to_string(id)+(imageType(data)==1?".png":".jpg");imageTotal+=data.size();zip.add("OEBPS/"+name,data);return name;
  }
  void tag(const Tag& t,size_t end){
    const auto& n=t.name;
    if(!hidden.empty()){if(t.closing&&n==hidden)hidden.clear();return;}
    if(!t.closing&&(n=="head"||n=="script"||n=="style"||n=="title"||n=="iframe"||n=="object"||n=="svg")){hidden=n;return;}
    if(!t.closing&&n=="mbp:pagebreak"){split();markAnchors(end);return;}
    if(block(n)){if(t.closing)closeBlock();else {startBlock(heading(n)?n:"p",n=="center"?"center":t.get("align"));if(n=="li")append("&#8226; ");}markAnchors(end);return;}
    markAnchors(end);
    if(!t.closing&&(n=="br"||n=="hr")){ensureBlock();append(n=="br"?"<br/>":"<br/>----<br/>");return;}
    if(!t.closing&&n=="img"){
      const auto file=image(number(t.get("recindex")));ensureBlock();if(file.empty())text(t.get("alt"));else {append("<img src=\""+file+"\" alt=\""+escape(t.get("alt"),book.encoding,true)+"\"/>");current().visible=true;}return;
    }
    const auto name=inlineName(n);if(name.empty())return;
    if(t.closing){for(size_t i=inlines.size();i>0;--i)if(inlines[i-1]==name){closeInline(i-1);break;}return;}
    ensureBlock();if(inlines.size()>=32)return;
    if(name=="a")for(size_t i=inlines.size();i>0;--i)if(inlines[i-1]=="a"){closeInline(i-1);break;}
    const size_t pos=number(t.get("filepos"));if(name=="a"&&targets.count(pos))current().links.push_back({current().body.size()+2,pos});
    append("<"+name+">");inlines.push_back(name);
  }
  bool parse(){
    if(book.html){
      scan(book.text,[&](size_t,size_t,std::string_view,const Tag* t){if(t&&!t->closing&&t->name=="a"&&targets.size()<4096){const size_t pos=number(t->get("filepos"));if(pos<book.text.size())targets.insert(pos);}});
      next=targets.begin();
      scan(book.text,[&](size_t start,size_t end,std::string_view raw,const Tag* t){if(failed)return;if(t)tag(*t,end);else if(hidden.empty()){markAnchors(start);markAnchors(end);text(raw);}});
    } else {
      next=targets.begin();std::string_view raw(book.text);while(!raw.empty()&&!failed){const auto e=raw.find('\n');const auto line=raw.substr(0,e);startBlock();text(line,false);closeBlock();if(e==raw.npos)break;raw.remove_prefix(e+1);}
    }
    markAnchors(book.text.size());closeBlock();
    // Keep trailing anchor-only content rather than leaving broken references.
    if(chapters.size()>1&&!current().visible&&current().body.empty())chapters.pop_back();
    return !failed&&std::any_of(chapters.begin(),chapters.end(),[](const Chapter& c){return c.visible;});
  }
  bool package(){
    zip.add("mimetype","application/epub+zip");zip.add("META-INF/container.xml","<?xml version=\"1.0\"?><container version=\"1.0\" xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile full-path=\"OEBPS/content.opf\" media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
    if(!parse())return false;
    const auto cover=image(book.coverIndex);std::string().swap(book.text);
    std::string manifest,spine,nav,coverMeta;
    const auto title=escape(book.title);
    for(size_t i=0;i<chapters.size();++i){const auto& c=chapters[i];const auto file=chapterName(i),id="c"+std::to_string(i);std::string x="<html xmlns=\"http://www.w3.org/1999/xhtml\"><head><title>"+title+"</title></head><body>";size_t from=0;
      for(const auto& link:c.links){const auto a=anchors.find(link.target);if(a==anchors.end())continue;x.append(c.body,from,link.at-from);x+=" href=\""+chapterName(a->second)+"#pos"+std::to_string(link.target)+"\"";from=link.at;}
      x.append(c.body,from,std::string::npos);x+="</body></html>";zip.add("OEBPS/"+file,x);
      manifest+="<item id=\""+id+"\" href=\""+file+"\" media-type=\"application/xhtml+xml\"/>";spine+="<itemref idref=\""+id+"\"/>";
      nav+="<navPoint id=\"n"+id+"\" playOrder=\""+std::to_string(i+1)+"\"><navLabel><text>"+(c.label.empty()?"Part "+std::to_string(i+1):escape(c.label))+"</text></navLabel><content src=\""+file+"\"/></navPoint>";
    }
    for(const auto& [index,name]:images)if(!name.empty()){const auto id="im"+std::to_string(index);manifest+="<item id=\""+id+"\" href=\""+name+"\" media-type=\""+(name.ends_with(".png")?"image/png":"image/jpeg")+"\"/>";if(name==cover)coverMeta="<meta name=\"cover\" content=\""+id+"\"/>";}
    zip.add("OEBPS/content.opf","<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" version=\"2.0\" unique-identifier=\"id\"><metadata xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:title>"+title+"</dc:title><dc:creator>"+escape(book.author)+"</dc:creator><dc:publisher>"+escape(book.publisher)+"</dc:publisher><dc:language>"+escape(book.language.empty()?"und":book.language)+"</dc:language><dc:identifier id=\"id\">books-mobi</dc:identifier>"+coverMeta+"</metadata><manifest><item id=\"ncx\" href=\"toc.ncx\" media-type=\"application/x-dtbncx+xml\"/>"+manifest+"</manifest><spine toc=\"ncx\">"+spine+"</spine></package>");
    zip.add("OEBPS/toc.ncx","<?xml version=\"1.0\" encoding=\"utf-8\"?><ncx xmlns=\"http://www.daisy.org/z3986/2005/ncx/\" version=\"2005-1\"><head/><docTitle><text>"+title+"</text></docTitle><navMap>"+nav+"</navMap></ncx>");
    return zip.finish();
  }
};
inline std::string cachePath(const std::string& path,const std::string& dir){
  struct stat st{};if(::lstat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode)||st.st_size<78)return {};
  uint64_t hash=14695981039346656037ull;const auto mix=[&](uint64_t v){for(int i=0;i<8;++i){hash^=(v>>(i*8))&255u;hash*=1099511628211ull;}};
  for(unsigned char c:path){hash^=c;hash*=1099511628211ull;}mix(st.st_size);mix(st.st_mtime);mix(st.st_ctime);mix(st.st_mtim.tv_nsec);mix(st.st_ctim.tv_nsec);
  char name[48];std::snprintf(name,sizeof name,"/mobi-r2-%016llx.epub",static_cast<unsigned long long>(hash));return dir+name;
}
inline bool validCache(const std::string& path){auto f=openSource(path);struct stat st{};std::string start,end;return f&&::fstat(::fileno(f.get()),&st)==0&&S_ISREG(st.st_mode)&&st.st_size>=26&&st.st_size<=64ll*1024*1024&&readAt(f.get(),0,4,start)&&start=="PK\x03\x04"&&readAt(f.get(),static_cast<size_t>(st.st_size)-22,22,end)&&end.starts_with("PK\x05\x06")&&end[20]==0&&end[21]==0;}
} // namespace books::mobipkg
namespace books {
inline std::string packageMobiBook(const std::string& path,const std::string& dir,std::string& error){
  using namespace mobipkg;const auto target=cachePath(path,dir);if(target.empty()){error="Invalid MOBI/PRC source";return {};}
  if(validCache(target))return target;
  Book book;if(!readBook(path,book,error,false,false))return {};
  auto source=openSource(path);if(!source){error="Source could not be reopened";return {};}
  ::mkdir(dir.c_str(),0755);std::string temp=target+".partXXXXXX";const int fd=::mkstemp(temp.data());if(fd<0){error="Cache not writable";return {};}
  FILE* f=::fdopen(fd,"wb");if(!f){::close(fd);::unlink(temp.c_str());error="Cache not writable";return {};}
  Zip zip(f);Import importer(book,source.get(),zip);const bool built=importer.package();const bool closed=std::fclose(f)==0;
  if(!built||!closed||cachePath(path,dir)!=target||::rename(temp.c_str(),target.c_str())!=0){::unlink(temp.c_str());error=built?"Cache write failed or source changed":"MOBI markup or package exceeded limits";return {};}
  return target;
}
} // namespace books
