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
  const std::string tempDir = path.substr(0,pos);
  const std::string basePath = "OEBPS/";

  BookMetadataCache cache(tempDir);
  assert(cache.beginWrite());
  assert(cache.beginContentOpfPass());
  BookMetadataCache::BookMetadata metadata;
  {
    ContentOpfParser parser(tempDir, basePath, n, &cache, false);
    assert(parser.setup());
    assert(zip.readFileToStream("OEBPS/content.opf", parser, 29));
    assert(parser.title == "CrossPoint multilingual test");
    assert(parser.author == "Books Test Author");
    assert(parser.language == "mul");
    metadata.title = parser.title;
    metadata.author = parser.author;
    metadata.language = parser.language;
    metadata.coverItemHref = parser.coverItemHref;
    metadata.textReferenceHref = parser.textReferenceHref;
  }
  assert(cache.endContentOpfPass());
  assert(cache.getSpineCount() == 2);
  assert(cache.beginTocPass());
  assert(cache.endTocPass());
  assert(cache.endWrite());
  assert(cache.buildBookBin(path, metadata));
  assert(cache.load());
  assert(cache.getSpineCount() == 2);
  assert(cache.getSpineEntry(0).href == "OEBPS/chapter1.xhtml");
  assert(cache.getSpineEntry(1).href == "OEBPS/chapter2.xhtml");
  assert(cache.getCumulativeSize(1) >= cache.getCumulativeSize(0));
  assert(cache.coreMetadata.language == "mul");
  assert(cache.cleanupTmpFiles());
  return 0;
}
