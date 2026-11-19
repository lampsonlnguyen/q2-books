#include <cassert>
#include <string>
#include <ZipFile.h>
#include <ContentOpfParser.h>
#include <Epub/BookMetadataCache.h>
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string path = argv[1];
  ZipFile zip(path);
  size_t n = 0;
  assert(zip.getInflatedFileSize("OEBPS/content.opf", &n));
  const size_t pos = path.find_last_of('/');
  const std::string tempDir = path.substr(0, pos);
  const std::string basePath = "OEBPS/";
  BookMetadataCache cache;
  ContentOpfParser parser(tempDir, basePath, n, &cache, false);
  assert(parser.setup());
  assert(zip.readFileToStream("OEBPS/content.opf", parser, 29));
  assert(parser.title == "CrossPoint multilingual test");
  assert(parser.author == "CrossPod Test Author");
  assert(parser.language == "mul");
  assert(cache.spine.size() == 2);
  assert(cache.spine.at(0) == "OEBPS/chapter1.xhtml");
  assert(cache.spine.at(1) == "OEBPS/chapter2.xhtml");
  return 0;
}
