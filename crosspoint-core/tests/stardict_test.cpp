// DI-01/DI-02: StarDict lookup over plain and dictzip dictionaries, synonyms,
// the sidecar index, and malformed-file fuzzing (run under ASan in CI).
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "StarDict.hpp"

using crosspod::stardict::Dictionary;

static std::string firstText(Dictionary& d,const std::string& word) {
  const auto r=d.lookup(word);
  return r.empty()?std::string():r[0].second;
}
static std::vector<char> slurp(const std::string& p) {
  std::ifstream f(p,std::ios::binary);
  return {std::istreambuf_iterator<char>(f),{}};
}
static void spit(const std::string& p,const std::vector<char>& d) {
  std::ofstream f(p,std::ios::binary|std::ios::trunc);
  f.write(d.data(),static_cast<std::streamsize>(d.size()));
}
static void copyDict(const std::string& from,const std::string& to,const char* name) {
  ::mkdir(to.c_str(),0755);
  for(const char* ext:{".ifo",".idx",".dict",".dict.dz",".syn"}) {
    const std::string src=from+"/"+name+ext;
    if(::access(src.c_str(),R_OK)==0)spit(to+"/"+name+ext,slurp(src));
  }
}

int main(int argc,char** argv) {
  assert(argc==2);
  const std::string fixtures=argv[1];
  char tmp[]="/tmp/crosspod-sd-XXXXXX";
  assert(mkdtemp(tmp));
  const std::string cache=std::string(tmp)+"/cache";

  assert(crosspod::stardict::stardictCompare("apple","Apple")>0);
  assert(crosspod::stardict::asciiCaseCompare("APPLE","apple")==0);

  {
    Dictionary plain;
    assert(plain.open(fixtures+"/plain/plain.ifo",cache));
    assert(plain.name=="Plain test dictionary");
    assert(plain.wordCount()==3000);
    assert(plain.sampleBytes()<=(3000/32+2)*4+64);  // bounded: samples only
    const auto apple=plain.lookup("apple");
    assert(apple.size()==2);  // "Apple" and "apple": case-insensitive matches
    assert(firstText(plain,"APPLE").find("A ")==0);
    assert(firstText(plain,"aardvark")=="The first word.");
    assert(firstText(plain,"zzz")=="The last word.");
    assert(firstText(plain,"word1234")=="Definition number 1234 of the plain dictionary.");
    assert(firstText(plain,"sách")=="Quyển sách (book).");
    assert(firstText(plain,"Đà Lạt")=="Thành phố ở Lâm Đồng.");
    assert(firstText(plain,"中文")=="Chinese language.");
    assert(firstText(plain,"color")=="British spelling of color.");  // .syn
    assert(firstText(plain,"book")=="Quyển sách (book).");
    assert(plain.lookup("pomme").size()==1);   // .syn points at one entry
    assert(plain.lookup("word9999").empty());
    assert(plain.lookup("").empty());
    assert(plain.lookup(std::string(300,'a')).empty());
  }
  // The sidecar was written, and a second open uses it.
  {
    Dictionary again;
    assert(again.open(fixtures+"/plain/plain.ifo",cache));
    assert(firstText(again,"zebra")=="A striped animal.");
  }
  {
    Dictionary zipped;
    assert(zipped.open(fixtures+"/zipped/zipped.ifo",cache));
    assert(firstText(zipped,"entry0007")=="Bold & text 7 plain 7");
    assert(firstText(zipped,"ENTRY1198")=="Bold & text 1198 plain 1198");
    const std::string longText=firstText(zipped,"longword");
    assert(longText.size()>13000 && longText.find("crosses dictzip chunks.")!=std::string::npos);
  }

  // Validation: each malformation is refused with a reason, not trusted.
  const std::string work=std::string(tmp)+"/bad";
  const auto expectRefused=[&](const char* what,auto mutate) {
    copyDict(fixtures+"/plain",work,"plain");
    mutate(work+"/plain");
    Dictionary d;
    const bool opened=d.open(work+"/plain.ifo",std::string());
    if(opened)std::fprintf(stderr,"accepted: %s\n",what);
    assert(!opened);
    assert(!d.error.empty());
  };
  expectRefused("idx size",[](const std::string& b){auto d=slurp(b+".idx");d.pop_back();spit(b+".idx",d);});
  expectRefused("wordcount",[](const std::string& b){
    auto ifo=slurp(b+".ifo");std::string s(ifo.begin(),ifo.end());
    s.replace(s.find("wordcount=3000"),14,"wordcount=3001");spit(b+".ifo",{s.begin(),s.end()});});
  expectRefused("offset",[](const std::string& b){
    auto d=slurp(b+".idx");  // first entry: "aardvark\0" then offset
    const size_t at=std::string(d.begin(),d.end()).find('\0')+1;
    d[at]=0x7f;spit(b+".idx",d);});
  expectRefused("unsorted",[](const std::string& b){
    auto d=slurp(b+".idx");d[0]='z';spit(b+".idx",d);});
  expectRefused("magic",[](const std::string& b){spit(b+".ifo",{'x','y'});});
  expectRefused("no data",[](const std::string& b){std::remove((b+".dict").c_str());});
  expectRefused("huge count",[](const std::string& b){
    auto ifo=slurp(b+".ifo");std::string s(ifo.begin(),ifo.end());
    s.replace(s.find("wordcount=3000"),14,"wordcount=99999999999");spit(b+".ifo",{s.begin(),s.end()});});

  // Fuzz: random byte changes and truncation in every file of both
  // dictionaries. Opening and looking up must never crash or overrun.
  uint32_t seed=0x51A2D1C7u;
  const auto rnd=[&]{seed=seed*1664525u+1013904223u;return seed>>8;};
  int opened=0;
  for(int round=0;round<400;++round) {
    const bool z=round%2;
    const char* name=z?"zipped":"plain";
    copyDict(fixtures+"/"+name,work,name);
    const char* exts[]={".ifo",".idx",z?".dict.dz":".dict",".syn"};
    const std::string file=work+"/"+name+exts[rnd()%(z?3:4)];
    auto data=slurp(file);
    if(data.empty())continue;
    if(rnd()%5==0)data.resize(rnd()%data.size());
    else for(int k=1+static_cast<int>(rnd()%8);k>0;--k)data[rnd()%data.size()]=static_cast<char>(rnd());
    spit(file,data);
    Dictionary d;
    if(!d.open(work+"/"+name+".ifo",std::string()))continue;
    ++opened;
    for(const char* w:{"apple","zzz","color","entry0500","longword","word0001","sách"})
      for(const auto& m:d.lookup(w))assert(m.second.size()<=crosspod::stardict::kMaxEntryBytes*2);
  }
  std::printf("CrossPod StarDict: PASS (fuzz: %d of 400 damaged dictionaries still opened)\n",opened);
  std::system((std::string("rm -rf ")+tmp).c_str());
  return 0;
}
