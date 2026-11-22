// SPDX-License-Identifier: MIT
#pragma once
// DI-01/DI-02 StarDict dictionaries (.ifo/.idx/.dict/.dict.dz/.syn).
//
// Memory is bounded: the .idx (and .syn) are never loaded whole. A sparse
// sidecar index (".cpidx": every 32nd entry's byte position) is built once
// per dictionary into the cache, keyed on the source files' size and mtime,
// written to a temporary name and renamed into place. A lookup binary-searches
// those samples by reading single words from the .idx, then scans at most 32
// entries. Definitions are read by offset from the .dict, or from a dictzip
// .dict.dz by inflating only the chunks that hold them.
//
// Every field read from the files is checked before use: header values,
// word lengths, offsets and sizes against the files' real sizes, the
// dictzip chunk table, and the output size. A malformed dictionary is
// skipped with a reason, never trusted.
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace books {
namespace stardict {

inline constexpr uint64_t kMaxWords=8u*1000u*1000u;        // per dictionary
inline constexpr uint64_t kMaxIdxBytes=512ull*1024*1024;
inline constexpr uint32_t kMaxEntryBytes=1u*1024u*1024u;   // one definition, raw
inline constexpr size_t kMaxDefinitionText=6000;           // shown text, per lookup
inline constexpr int kSampleEvery=32;
inline constexpr size_t kMaxWordBytes=255;                 // StarDict's limit (<256)
inline constexpr int kMaxMatches=4;                        // entries per word, per dictionary
inline constexpr int kSidecarVersion=1;

struct Info {
  std::string bookname;
  std::string sameTypeSequence;
  uint64_t wordCount=0;
  uint64_t synWordCount=0;
  uint64_t idxFileSize=0;
  int offsetBits=32;
};

// ASCII-case-insensitive, then byte order: StarDict's stardict_strcmp,
// the order .idx and .syn are sorted in.
inline int asciiCaseCompare(std::string_view a,std::string_view b) {
  const size_t n=std::min(a.size(),b.size());
  for(size_t i=0;i<n;++i) {
    unsigned char x=static_cast<unsigned char>(a[i]),y=static_cast<unsigned char>(b[i]);
    if(x>='A'&&x<='Z')x+=32;
    if(y>='A'&&y<='Z')y+=32;
    if(x!=y)return x<y?-1:1;
  }
  return a.size()==b.size()?0:(a.size()<b.size()?-1:1);
}
inline int stardictCompare(std::string_view a,std::string_view b) {
  const int c=asciiCaseCompare(a,b);
  if(c)return c;
  return a.compare(b)<0?-1:(a==b?0:1);
}

inline bool parseUnsigned(const std::string& s,uint64_t& out) {
  if(s.empty()||s.size()>19)return false;
  uint64_t v=0;
  for(char c:s){if(c<'0'||c>'9')return false;v=v*10+static_cast<uint64_t>(c-'0');}
  out=v;return true;
}

inline bool parseIfo(const std::string& path,Info& info,std::string& error) {
  FILE* f=std::fopen(path.c_str(),"rb");
  if(!f){error="cannot open .ifo";return false;}
  char buffer[8192];
  const size_t n=std::fread(buffer,1,sizeof(buffer),f);
  std::fclose(f);
  std::string text(buffer,n);
  if(text.rfind("\xEF\xBB\xBF",0)==0)text.erase(0,3);
  if(text.rfind("StarDict's dict ifo file",0)!=0){error="not a StarDict .ifo";return false;}
  bool version=false,count=false,size=false;
  size_t pos=0;
  while(pos<text.size()) {
    size_t end=text.find('\n',pos);
    if(end==std::string::npos)end=text.size();
    std::string line=text.substr(pos,end-pos);
    pos=end+1;
    if(!line.empty()&&line.back()=='\r')line.pop_back();
    const size_t eq=line.find('=');
    if(eq==std::string::npos)continue;
    const std::string key=line.substr(0,eq),value=line.substr(eq+1);
    uint64_t number=0;
    if(key=="version") {
      version=value=="2.4.2"||value=="3.0.0";
      if(!version){error="unsupported .ifo version";return false;}
    } else if(key=="wordcount") {
      if(!parseUnsigned(value,number)||number==0||number>kMaxWords){error="bad wordcount";return false;}
      info.wordCount=number;count=true;
    } else if(key=="synwordcount") {
      if(!parseUnsigned(value,number)||number>kMaxWords){error="bad synwordcount";return false;}
      info.synWordCount=number;
    } else if(key=="idxfilesize") {
      if(!parseUnsigned(value,number)||number==0||number>kMaxIdxBytes){error="bad idxfilesize";return false;}
      info.idxFileSize=number;size=true;
    } else if(key=="idxoffsetbits") {
      if(value!="32"&&value!="64"){error="bad idxoffsetbits";return false;}
      info.offsetBits=value=="64"?64:32;
    } else if(key=="bookname") {
      info.bookname=value.substr(0,80);
    } else if(key=="sametypesequence") {
      if(value.size()>16){error="bad sametypesequence";return false;}
      info.sameTypeSequence=value;
    }
  }
  if(!version||!count||!size){error="missing .ifo field";return false;}
  return true;
}

inline uint32_t be32(const unsigned char* p) {
  return (uint32_t(p[0])<<24)|(uint32_t(p[1])<<16)|(uint32_t(p[2])<<8)|uint32_t(p[3]);
}

// A file read by offset, with its size checked once.
struct RandomFile {
  FILE* f=nullptr;
  uint64_t size=0;
  ~RandomFile(){if(f)std::fclose(f);}
  bool open(const std::string& path) {
    struct stat st{};
    if(::stat(path.c_str(),&st)!=0||!S_ISREG(st.st_mode))return false;
    f=std::fopen(path.c_str(),"rb");
    size=static_cast<uint64_t>(st.st_size);
    return f!=nullptr;
  }
  bool read(uint64_t offset,void* out,size_t n) {
    if(!f||offset>size||n>size-offset)return false;
    if(fseeko(f,static_cast<off_t>(offset),SEEK_SET)!=0)return false;
    return std::fread(out,1,n,f)==n;
  }
};

// One entry of a sorted word list (.idx: word, offset, size; .syn: word, index).
struct Entry {
  std::string word;
  uint64_t a=0;   // .idx: data offset; .syn: .idx entry number
  uint32_t b=0;   // .idx: data size
  uint64_t next=0;  // byte position of the following entry
};
// Read the entry at |pos|. |tail| is the bytes after the NUL: 8 or 12 (.idx),
// or 4 (.syn).
inline bool readEntry(RandomFile& file,uint64_t limit,uint64_t pos,int tail,Entry& e) {
  if(pos>=limit)return false;
  unsigned char buf[kMaxWordBytes+1+12];
  const size_t want=static_cast<size_t>(std::min<uint64_t>(sizeof(buf),limit-pos));
  if(!file.read(pos,buf,want))return false;
  const void* nul=std::memchr(buf,0,std::min(want,kMaxWordBytes+1));
  if(!nul)return false;
  const size_t len=static_cast<const unsigned char*>(nul)-buf;
  if(len==0||len+1+tail>want)return false;
  e.word.assign(reinterpret_cast<char*>(buf),len);
  const unsigned char* p=buf+len+1;
  if(tail==4){e.a=be32(p);e.b=0;}
  else if(tail==8){e.a=be32(p);e.b=be32(p+4);}
  else {e.a=(uint64_t(be32(p))<<32)|be32(p+4);e.b=be32(p+8);}
  e.next=pos+len+1+tail;
  return true;
}

// The sparse sample table: byte position of every kSampleEvery-th entry.
struct Samples {
  std::vector<uint32_t> pos;  // the files are <= 512 MB
  uint64_t entries=0;
};

inline uint64_t fnv(uint64_t h,uint64_t v) {
  for(int i=0;i<8;++i){h^=(v>>(8*i))&255u;h*=1099511628211ull;}
  return h;
}

// Walk a word list once to collect samples, checking every entry and the
// sort order. |dataSize| bounds .idx offsets; |idxCount| bounds .syn indexes.
inline bool scanList(RandomFile& file,uint64_t limit,int tail,uint64_t expected,
                     uint64_t dataSize,uint64_t idxCount,Samples& out,std::string& error) {
  out.pos.clear();out.entries=0;
  // Read in blocks rather than per entry: one pass over the file.
  std::vector<unsigned char> block(64*1024);
  uint64_t blockStart=0;size_t blockLen=0;
  uint64_t pos=0;
  std::string previous;
  while(pos<limit) {
    if(out.entries>=expected){error="more entries than declared";return false;}
    if(pos+kMaxWordBytes+1+12>blockStart+blockLen && blockStart+blockLen<limit) {
      blockStart=pos;
      blockLen=static_cast<size_t>(std::min<uint64_t>(block.size(),limit-pos));
      if(!file.read(blockStart,block.data(),blockLen)){error="read failed";return false;}
    }
    const unsigned char* p=block.data()+(pos-blockStart);
    const size_t avail=static_cast<size_t>(blockStart+blockLen-pos);
    const void* nul=std::memchr(p,0,std::min(avail,kMaxWordBytes+1));
    if(!nul){error="word without end or too long";return false;}
    const size_t len=static_cast<const unsigned char*>(nul)-p;
    if(len==0||len+1+tail>avail){error="truncated entry";return false;}
    const std::string_view word(reinterpret_cast<const char*>(p),len);
    const unsigned char* t=p+len+1;
    if(tail==4) {
      if(be32(t)>=idxCount){error="synonym index out of range";return false;}
    } else {
      const uint64_t off=tail==8?be32(t):((uint64_t(be32(t))<<32)|be32(t+4));
      const uint32_t size=be32(t+(tail==8?4:8));
      if(size>kMaxEntryBytes||off>dataSize||size>dataSize-off){error="definition outside .dict";return false;}
    }
    // Sorted lists only: binary search depends on it. Allow equal neighbours.
    if(!previous.empty()&&asciiCaseCompare(previous,word)>0){error="word list not sorted";return false;}
    previous.assign(word);
    if(out.entries%kSampleEvery==0)out.pos.push_back(static_cast<uint32_t>(pos));
    ++out.entries;
    pos+=len+1+tail;
  }
  if(out.entries!=expected){error="fewer entries than declared";return false;}
  return true;
}

// Dictzip (.dict.dz): gzip whose FEXTRA "RA" field lists the compressed size
// of each fixed-size uncompressed chunk; each chunk inflates on its own.
struct DictZip {
  RandomFile file;
  uint32_t chunkLength=0;
  std::vector<uint64_t> chunkOffset;  // compressed start of each chunk, plus end
  uint64_t uncompressed=0;
  bool open(const std::string& path,std::string& error) {
    if(!file.open(path)){error="cannot open .dict.dz";return false;}
    unsigned char h[12];
    if(!file.read(0,h,10)||h[0]!=0x1f||h[1]!=0x8b||h[2]!=8){error="not gzip";return false;}
    const int flags=h[3];
    if(!(flags&4)){error="gzip without dictzip table";return false;}
    unsigned char xl[2];
    if(!file.read(10,xl,2)){error="truncated header";return false;}
    const uint32_t xlen=xl[0]|(uint32_t(xl[1])<<8);
    std::vector<unsigned char> extra(xlen);
    if(xlen<10||!file.read(12,extra.data(),xlen)){error="truncated extra";return false;}
    uint64_t pos=12+xlen;
    bool found=false;
    for(size_t i=0;i+4<=extra.size();) {
      const uint32_t slen=extra[i+2]|(uint32_t(extra[i+3])<<8);
      if(i+4+slen>extra.size()){error="bad extra field";return false;}
      if(extra[i]=='R'&&extra[i+1]=='A'&&slen>=6) {
        const unsigned char* s=&extra[i+4];
        const uint32_t ver=s[0]|(uint32_t(s[1])<<8);
        chunkLength=s[2]|(uint32_t(s[3])<<8);
        const uint32_t count=s[4]|(uint32_t(s[5])<<8);
        if(ver!=1||chunkLength==0||count==0||6+2ull*count>slen){error="bad dictzip table";return false;}
        chunkOffset.resize(count+1);
        found=true;
        std::vector<uint32_t> sizes(count);
        for(uint32_t c=0;c<count;++c)sizes[c]=s[6+2*c]|(uint32_t(s[7+2*c])<<8);
        // Offsets are computed after the rest of the header is skipped.
        chunkOffset[0]=0;
        for(uint32_t c=0;c<count;++c)chunkOffset[c+1]=chunkOffset[c]+sizes[c];
      }
      i+=4+slen;
    }
    if(!found){error="gzip without dictzip table";return false;}
    const auto skipString=[&](uint64_t& at)->bool {
      unsigned char c=1;
      for(int guard=0;guard<4096;++guard){if(!file.read(at++,&c,1))return false;if(!c)return true;}
      return false;
    };
    if((flags&8)&&!skipString(pos)){error="bad gzip name";return false;}
    if((flags&16)&&!skipString(pos)){error="bad gzip comment";return false;}
    if(flags&2)pos+=2;
    for(auto& o:chunkOffset)o+=pos;
    if(chunkOffset.back()>file.size){error="dictzip chunks past end of file";return false;}
    uncompressed=uint64_t(chunkLength)*(chunkOffset.size()-1);  // upper bound
    return true;
  }
  // Inflate [offset, offset+size) of the uncompressed data.
  bool read(uint64_t offset,uint32_t size,std::string& out) {
    out.clear();
    if(!size)return true;
    if(offset>uncompressed||size>uncompressed-offset)return false;
    const uint64_t first=offset/chunkLength,last=(offset+size-1)/chunkLength;
    if(last+1>=chunkOffset.size())return false;
    std::string data;
    std::vector<unsigned char> in,chunk(chunkLength);
    for(uint64_t c=first;c<=last;++c) {
      const uint64_t start=chunkOffset[c],end=chunkOffset[c+1];
      if(end<start||end-start>4ull*chunkLength+1024)return false;
      in.resize(static_cast<size_t>(end-start));
      if(!in.empty()&&!file.read(start,in.data(),in.size()))return false;
      z_stream z{};
      if(inflateInit2(&z,-15)!=Z_OK)return false;
      z.next_in=in.data();z.avail_in=static_cast<uInt>(in.size());
      z.next_out=chunk.data();z.avail_out=static_cast<uInt>(chunk.size());
      const int rc=inflate(&z,Z_SYNC_FLUSH);
      const size_t got=chunk.size()-z.avail_out;
      inflateEnd(&z);
      if(rc!=Z_OK&&rc!=Z_STREAM_END&&rc!=Z_BUF_ERROR)return false;
      data.append(reinterpret_cast<char*>(chunk.data()),got);
      if(got<chunkLength&&c<last)return false;  // a short chunk before the end
    }
    const uint64_t skip=offset-first*chunkLength;
    if(skip>data.size()||size>data.size()-skip)return false;
    out.assign(data,static_cast<size_t>(skip),size);
    return true;
  }
};

// Plain text from one entry's fields: m/l/t/y text kept; h/g/x markup
// reduced to text; binary and resource fields skipped.
inline void appendMarkupText(std::string& out,std::string_view s) {
  bool tag=false;
  for(size_t i=0;i<s.size();++i) {
    const char c=s[i];
    if(tag){if(c=='>')tag=false;continue;}
    if(c=='<') {
      tag=true;
      // Line breaks for block tags.
      const std::string_view rest=s.substr(i+1,4);
      if(rest.rfind("br",0)==0||rest.rfind("p",0)==0||rest.rfind("/p",0)==0||
         rest.rfind("div",0)==0||rest.rfind("li",0)==0)out+=' ';
      continue;
    }
    if(c=='&') {
      static const struct {const char* name;const char* text;} entities[]={
        {"&amp;","&"},{"&lt;","<"},{"&gt;",">"},{"&quot;","\""},{"&apos;","'"},{"&nbsp;"," "}};
      bool matched=false;
      for(const auto& e:entities) {
        const size_t n=std::strlen(e.name);
        if(s.substr(i,n)==e.name){out+=e.text;i+=n-1;matched=true;break;}
      }
      if(matched)continue;
    }
    out+=c;
  }
}
inline std::string definitionText(std::string_view data,const std::string& types) {
  std::string out;
  const auto addField=[&](char type,std::string_view field) {
    if(!out.empty())out+=' ';
    if(type=='m'||type=='l'||type=='t'||type=='y'||type=='k'||type=='w')out.append(field);
    else if(type=='h'||type=='g'||type=='x')appendMarkupText(out,field);
  };
  size_t pos=0;
  if(!types.empty()) {
    for(size_t i=0;i<types.size()&&pos<=data.size();++i) {
      const char type=types[i];
      const bool last=i+1==types.size();
      if(type>='a'&&type<='z') {
        size_t end=last?data.size():data.find('\0',pos);
        if(end==std::string_view::npos)end=data.size();
        addField(type,data.substr(pos,end-pos));
        pos=end+1;
      } else {
        size_t n=data.size()-pos;
        if(!last) {
          if(data.size()-pos<4)break;
          n=be32(reinterpret_cast<const unsigned char*>(data.data()+pos));pos+=4;
          if(n>data.size()-pos)break;
        }
        pos+=n;  // binary: skipped
      }
    }
  } else {
    while(pos<data.size()) {
      const char type=data[pos++];
      if(type>='a'&&type<='z') {
        size_t end=data.find('\0',pos);
        if(end==std::string_view::npos)end=data.size();
        addField(type,data.substr(pos,end-pos));
        pos=end+1;
      } else {
        if(data.size()-pos<4)break;
        const uint32_t n=be32(reinterpret_cast<const unsigned char*>(data.data()+pos));pos+=4;
        if(n>data.size()-pos)break;
        pos+=n;
      }
    }
  }
  // Collapse whitespace runs; the overlay wraps by spaces.
  std::string clean;
  for(char c:out) {
    if(c=='\n'||c=='\r'||c=='\t')c=' ';
    if(c==' '&&(clean.empty()||clean.back()==' '))continue;
    clean+=c;
  }
  while(!clean.empty()&&clean.back()==' ')clean.pop_back();
  return clean;
}

class Dictionary {
 public:
  std::string name;   // bookname, else the file name
  std::string error;  // why it could not open
  bool open(const std::string& ifoPath,const std::string& cacheDir) {
    if(ifoPath.size()<5||ifoPath.compare(ifoPath.size()-4,4,".ifo")!=0){error="not .ifo";return false;}
    base_=ifoPath.substr(0,ifoPath.size()-4);
    if(!parseIfo(ifoPath,info_,error))return false;
    name=info_.bookname.empty()?base_.substr(base_.find_last_of('/')+1):info_.bookname;
    if(!idx_.open(base_+".idx")){error="missing .idx (a compressed .idx.gz is not supported)";return false;}
    if(idx_.size!=info_.idxFileSize){error=".idx size differs from .ifo";return false;}
    if(plain_.open(base_+".dict"))dataSize_=plain_.size;
    else {
      zip_=std::make_unique<DictZip>();
      if(!zip_->open(base_+".dict.dz",error)){error="dictionary data: "+error;return false;}
      dataSize_=zip_->uncompressed;
    }
    tail_=info_.offsetBits==64?12:8;
    if(!loadSamples(cacheDir))return false;
    if(info_.synWordCount && syn_.open(base_+".syn")) {
      std::string synError;
      if(!scanList(syn_,syn_.size,4,info_.synWordCount,0,info_.wordCount,synSamples_,synError))
        synSamples_={};  // a bad .syn only loses synonyms
    }
    return true;
  }
  // Up to kMaxMatches definitions for |word| (ASCII-case-insensitive, as
  // StarDict compares), headword first, then synonyms.
  std::vector<std::pair<std::string,std::string>> lookup(const std::string& word) {
    std::vector<std::pair<std::string,std::string>> found;
    if(word.empty()||word.size()>kMaxWordBytes)return found;
    std::vector<uint64_t> seen;
    const auto add=[&](const Entry& e) {
      if(found.size()>=static_cast<size_t>(kMaxMatches))return;
      if(std::find(seen.begin(),seen.end(),e.a)!=seen.end())return;
      seen.push_back(e.a);
      std::string raw;
      if(!readData(e.a,e.b,raw))return;
      std::string text=definitionText(raw,info_.sameTypeSequence);
      if(!text.empty())found.emplace_back(e.word,std::move(text));
    };
    forEachMatch(idx_,idx_.size,tail_,samples_,word,[&](const Entry& e){add(e);});
    if(!synSamples_.pos.empty()) {
      forEachMatch(syn_,syn_.size,4,synSamples_,word,[&](const Entry& s) {
        Entry target;
        if(entryAt(s.a,target))add(target);
      });
    }
    return found;
  }
  uint64_t wordCount() const {return info_.wordCount;}
  size_t sampleBytes() const {return (samples_.pos.size()+synSamples_.pos.size())*sizeof(uint32_t);}

 private:
  std::string base_;
  Info info_;
  RandomFile idx_,plain_,syn_;
  std::unique_ptr<DictZip> zip_;
  uint64_t dataSize_=0;
  int tail_=8;
  Samples samples_,synSamples_;

  bool readData(uint64_t offset,uint32_t size,std::string& out) {
    if(size>kMaxEntryBytes||offset>dataSize_||size>dataSize_-offset)return false;
    if(zip_)return zip_->read(offset,size,out);
    out.resize(size);
    return size==0||plain_.read(offset,&out[0],size);
  }
  // The .idx entry with this number, through the samples.
  bool entryAt(uint64_t number,Entry& e) {
    if(number>=samples_.entries)return false;
    uint64_t pos=samples_.pos[number/kSampleEvery];
    for(uint64_t i=number-number%kSampleEvery;;++i) {
      if(!readEntry(idx_,idx_.size,pos,tail_,e))return false;
      if(i==number)return true;
      pos=e.next;
    }
  }
  template <typename F>
  void forEachMatch(RandomFile& file,uint64_t limit,int tail,const Samples& s,
                    const std::string& word,F&& visit) {
    if(s.pos.empty())return;
    // Last sample whose word sorts before |word| (case-insensitively), so the
    // scan starts before the first equal entry.
    size_t lo=0,hi=s.pos.size();
    while(hi-lo>1) {
      const size_t mid=(lo+hi)/2;
      Entry e;
      if(!readEntry(file,limit,s.pos[mid],tail,e))return;
      if(asciiCaseCompare(e.word,word)<0)lo=mid;else hi=mid;
    }
    uint64_t pos=s.pos[lo];
    int matches=0;
    for(uint64_t i=0;i<s.entries && pos<limit;++i) {
      Entry e;
      if(!readEntry(file,limit,pos,tail,e))return;
      const int c=asciiCaseCompare(e.word,word);
      if(c>0)return;
      if(c==0) {
        visit(e);
        if(++matches>=kMaxMatches*2)return;
      } else if(i>=static_cast<uint64_t>(kSampleEvery)*2)return;  // sorted: no match ahead
      pos=e.next;
    }
  }
  std::string sidecarPath(const std::string& cacheDir) const {
    struct stat a{},b{};
    (void)::stat((base_+".idx").c_str(),&a);
    (void)::stat((base_+".ifo").c_str(),&b);
    uint64_t h=14695981039346656037ull;
    for(unsigned char c:base_){h^=c;h*=1099511628211ull;}
    h=fnv(h,static_cast<uint64_t>(a.st_size));h=fnv(h,static_cast<uint64_t>(a.st_mtime));
    h=fnv(h,static_cast<uint64_t>(b.st_size));h=fnv(h,static_cast<uint64_t>(b.st_mtime));
    h=fnv(h,static_cast<uint64_t>(dataSize_));h=fnv(h,kSidecarVersion);
    char leaf[48];
    std::snprintf(leaf,sizeof(leaf),"/sd-%016llx.cpidx",static_cast<unsigned long long>(h));
    return cacheDir+leaf;
  }
  bool loadSamples(const std::string& cacheDir) {
    const std::string sidecar=cacheDir.empty()?std::string():sidecarPath(cacheDir);
    const uint64_t expectSamples=(info_.wordCount+kSampleEvery-1)/kSampleEvery;
    if(!sidecar.empty()) {
      if(FILE* f=std::fopen(sidecar.c_str(),"rb")) {
        char magic[8]{};uint64_t entries=0,count=0;
        bool ok=std::fread(magic,1,8,f)==8&&std::memcmp(magic,"CPSDIDX1",8)==0&&
                std::fread(&entries,8,1,f)==1&&std::fread(&count,8,1,f)==1&&
                entries==info_.wordCount&&count==expectSamples;
        if(ok) {
          samples_.pos.resize(static_cast<size_t>(count));
          samples_.entries=entries;
          ok=std::fread(samples_.pos.data(),4,samples_.pos.size(),f)==samples_.pos.size();
          for(size_t i=0;ok&&i<samples_.pos.size();++i)
            ok=samples_.pos[i]<idx_.size&&(i==0?samples_.pos[i]==0:samples_.pos[i]>samples_.pos[i-1]);
        }
        std::fclose(f);
        if(ok)return true;
        samples_={};
      }
    }
    if(!scanList(idx_,idx_.size,tail_,info_.wordCount,dataSize_,0,samples_,error)) {
      error=".idx: "+error;return false;
    }
    if(!sidecar.empty()) {
      ::mkdir(cacheDir.c_str(),0755);
      // A build killed before its rename leaves "<sidecar>.part<pid>".
      if(DIR* d=opendir(cacheDir.c_str())) {
        const std::string prefix=sidecar.substr(sidecar.find_last_of('/')+1)+".part";
        std::vector<std::string> stale;
        for(dirent* e;(e=readdir(d))!=nullptr;)
          if(std::string_view(e->d_name).rfind(prefix,0)==0)stale.push_back(cacheDir+"/"+e->d_name);
        closedir(d);
        for(const auto& p:stale)std::remove(p.c_str());
      }
      const std::string tmp=sidecar+".part"+std::to_string(static_cast<long>(::getpid()));
      if(FILE* f=std::fopen(tmp.c_str(),"wb")) {
        const uint64_t entries=samples_.entries,count=samples_.pos.size();
        bool ok=std::fwrite("CPSDIDX1",1,8,f)==8&&std::fwrite(&entries,8,1,f)==1&&
                std::fwrite(&count,8,1,f)==1&&
                std::fwrite(samples_.pos.data(),4,samples_.pos.size(),f)==samples_.pos.size();
        ok=std::fclose(f)==0&&ok;
        if(!ok||std::rename(tmp.c_str(),sidecar.c_str())!=0)std::remove(tmp.c_str());
      }
    }
    return true;
  }
};

// .ifo files in |folder| and its direct subfolders (one dictionary per
// folder is the usual layout), sorted by path; at most |limit|.
inline std::vector<std::string> findDictionaries(const std::string& folder,size_t limit=16) {
  std::vector<std::string> found;
  const auto scan=[&](const std::string& dir,bool descend,auto& self)->void {
    DIR* d=opendir(dir.c_str());
    if(!d)return;
    std::vector<std::string> subdirs;
    for(dirent* e;(e=readdir(d))!=nullptr;) {
      if(e->d_name[0]=='.')continue;
      const std::string name=e->d_name;
      const std::string path=dir+"/"+name;
      struct stat st{};
      if(::lstat(path.c_str(),&st)!=0)continue;
      if(S_ISDIR(st.st_mode)&&descend)subdirs.push_back(path);
      else if(S_ISREG(st.st_mode)&&name.size()>4&&name.compare(name.size()-4,4,".ifo")==0)
        found.push_back(path);
    }
    closedir(d);
    std::sort(subdirs.begin(),subdirs.end());
    for(const auto& s:subdirs)self(s,false,self);
  };
  scan(folder,true,scan);
  std::sort(found.begin(),found.end());
  if(found.size()>limit)found.resize(limit);
  return found;
}

}  // namespace stardict
}  // namespace books
