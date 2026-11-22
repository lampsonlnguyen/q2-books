// RD-04: a .txt book is wrapped as a stored-ZIP EPUB. The parts must cover
// the text exactly, end on UTF-8 boundaries, and the package must be a ZIP
// the original CrossPoint ZipFile reads, with the OPF the engine opens.
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "TxtPackage.hpp"
#include "ZipFile.h"

static std::string readEntry(ZipFile& zip,const char* name) {
  size_t size=0;
  uint8_t* data=zip.readFileToMemory(name,&size,true);
  assert(data);
  std::string out(reinterpret_cast<char*>(data),size);
  std::free(data);
  return out;
}

int main() {
  // Splitting: exact coverage, bounded parts, no split inside a character.
  std::string text;
  for(int i=0;i<4000;++i)
    text+="Dòng thứ "+std::to_string(i)+" — tiếng Việt và 中文 text.\n"+(i%7==0?"\n":"");
  const auto parts=crosspod::txtpkg::splitParts(text);
  assert(parts.size()>4);
  std::string joined;
  for(auto p:parts) {
    assert(p.size()<=crosspod::kTxtPartBytes+crosspod::kTxtPartBytes/4);
    assert(crosspod::txtpkg::validUtf8(p));
    joined+=p;
  }
  assert(joined==text);
  // One long line with no newline still splits on character boundaries.
  std::string line;
  while(line.size()<200000)line+="ộ";
  for(auto p:crosspod::txtpkg::splitParts(line))assert(crosspod::txtpkg::validUtf8(p));
  // Legacy Latin-1 bytes become UTF-8 instead of invalid markup.
  assert(crosspod::txtpkg::latin1ToUtf8("caf\xe9")=="caf\xc3\xa9");
  assert(crosspod::txtpkg::crc32("123456789")==0xCBF43926u);

  char dir[]="/tmp/crosspod-txt-XXXXXX";
  assert(mkdtemp(dir));
  const std::string book=std::string(dir)+"/A <Story> & more.TXT";
  {
    FILE* f=std::fopen(book.c_str(),"wb");
    assert(f);
    assert(std::fwrite(text.data(),1,text.size(),f)==text.size());
    assert(std::fclose(f)==0);
  }
  std::string error;
  const std::string cache=std::string(dir)+"/cache";
  // A killed build's leftovers for this book are removed by the next build.
  std::string package=crosspod::packageTxtBook(book,cache,error);
  assert(!package.empty());
  std::remove(package.c_str());
  const std::string stale=package+".part424242";
  {FILE* f=std::fopen(stale.c_str(),"wb");assert(f);std::fputs("half",f);std::fclose(f);}
  package=crosspod::packageTxtBook(book,cache,error);
  assert(!package.empty() && ::access(stale.c_str(),F_OK)!=0);
  struct stat first{};
  assert(::stat(package.c_str(),&first)==0);
  assert(crosspod::packageTxtBook(book,cache,error)==package);  // reused, not rebuilt

  ZipFile zip(package);
  assert(zip.loadAllFileStatSlims());
  assert(readEntry(zip,"mimetype")=="application/epub+zip");
  assert(readEntry(zip,"META-INF/container.xml").find("OEBPS/content.opf")!=std::string::npos);
  const std::string opf=readEntry(zip,"OEBPS/content.opf");
  assert(opf.find("<dc:title>A &lt;Story&gt; &amp; more</dc:title>")!=std::string::npos);
  assert(opf.find("idref=\"t0000\"")!=std::string::npos);
  const std::string part=readEntry(zip,"OEBPS/t0000.xhtml");
  assert(part.find("Dòng thứ 0")!=std::string::npos);
  assert(part.find("<body>")!=std::string::npos);

  // Over the size bound: refused, with no package written.
  const std::string big=std::string(dir)+"/big.txt";
  {
    FILE* f=std::fopen(big.c_str(),"wb");
    assert(f);
    assert(std::fseek(f,static_cast<long>(crosspod::kTxtMaxBytes),SEEK_SET)==0);
    assert(std::fputc('x',f)=='x');
    assert(std::fclose(f)==0);
  }
  assert(crosspod::packageTxtBook(big,cache,error).empty());
  assert(crosspod::isTxtFilename("x.TxT") && !crosspod::isTxtFilename("x.txt.bak"));
  assert(crosspod::isBookFilename("x.epub") && crosspod::isBookFilename("y.txt"));

  std::remove(package.c_str());
  std::remove(book.c_str());
  std::remove(big.c_str());
  rmdir(cache.c_str());
  assert(rmdir(dir)==0);
  std::puts("CrossPod TXT package: PASS");
  return 0;
}
