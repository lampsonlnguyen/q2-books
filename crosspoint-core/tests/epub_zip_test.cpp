#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include "ZipFile.h"
#include "ContainerParser.h"

class Collect final : public Print {
 public:
  std::string value;
  size_t write(uint8_t b) override { value.push_back(static_cast<char>(b)); return 1; }
  size_t write(const uint8_t* bytes, size_t n) override {
    if (value.size()+n > (1u << 20)) return 0;
    value.append(reinterpret_cast<const char*>(bytes), n);
    return n;
  }
};
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string filename = argv[1];
  ZipFile zip(filename);
  size_t n = 0;
  assert(zip.getInflatedFileSize("mimetype",&n));
  assert(n == strlen("application/epub+zip"));
  Collect mime;
  assert(zip.readFileToStream("mimetype",mime,19));
  assert(mime.value == "application/epub+zip");

  assert(zip.getInflatedFileSize("META-INF/container.xml",&n));
  ContainerParser container(n);
  assert(container.setup());
  assert(zip.readFileToStream("META-INF/container.xml",container,17));
  assert(container.fullPath == "OEBPS/content.opf");

  Collect opf;
  assert(zip.readFileToStream(container.fullPath.c_str(),opf,33));
  assert(opf.value.find("itemref idref=\"c1\"") != std::string::npos);

  assert(zip.loadAllFileStatSlims());
  for (const char* entry : {"OEBPS/chapter1.xhtml", "OEBPS/chapter2.xhtml"}) {
    auto* bytes = zip.readFileToMemory(entry,&n,true);
    assert(bytes != nullptr);
    assert(strlen(reinterpret_cast<const char*>(bytes))==n);
    assert(strstr(reinterpret_cast<const char*>(bytes), "<body>") != nullptr);
    std::free(bytes);
  }
  Collect chapter;
  assert(zip.readFileToStream("OEBPS/chapter1.xhtml",chapter,41));
  assert(chapter.value.find("Nguyễn") != std::string::npos);
  assert(chapter.value.find("中文") != std::string::npos);
  assert(chapter.value.find("مرحبا") != std::string::npos);
  assert(chapter.value.find("שלום") != std::string::npos);
  assert(!zip.getInflatedFileSize("OEBPS/not-here.xhtml",&n));
  return 0;
}
