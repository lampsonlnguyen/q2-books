#include <cassert>
#include <string>
#include <ZipFile.h>
#include <ContentOpfParser.h>
#include <Epub/BookMetadataCache.h>
int main(int argc, char** argv) {
  assert(argc == 2);
  const auto checkSeries = [](const std::string& meta, const std::string& title,
                              std::optional<float> volume) {
    const std::string xml = "<package xmlns=\"http://www.idpf.org/2007/opf\"><metadata>" +
        meta + "</metadata><manifest/><spine/></package>";
    ContentOpfParser parser("", "", xml.size(), nullptr, false);
    assert(parser.setup());
    assert(parser.write(reinterpret_cast<const uint8_t*>(xml.data()), xml.size()) == xml.size());
    assert(parser.series == title);
    assert(parser.seriesIndex == volume);
  };
  checkSeries("<meta name=\"calibre:series\" content=\"  Saga   One  \"/>"
              "<meta name=\"calibre:series_index\" content=\" 2.5 \"/>","Saga One",2.5f);
  checkSeries("<meta refines=\"#s\" property=\"group-position\">10</meta>"
              "<meta refines=\"#s\" property=\"collection-type\">series</meta>"
              "<meta id=\"s\" property=\"belongs-to-collection\">Saga</meta>","Saga",10.f);
  checkSeries("<meta id=\"s\" property=\"belongs-to-collection\">Saga</meta>"
              "<meta refines=\"#s\" property=\"collection-type\">set</meta>","",std::nullopt);
  checkSeries("<meta name=\"calibre:series\" content=\"Saga\"/>"
              "<meta name=\"calibre:series_index\" content=\"NaN\"/>","Saga",std::nullopt);
  checkSeries("<meta name=\"calibre:series\" content=\"Saga\"/>"
              "<meta id=\"s\" property=\"belongs-to-collection\">Saga</meta>"
              "<meta refines=\"#s\" property=\"collection-type\">series</meta>"
              "<meta refines=\"#s\" property=\"group-position\">0.5</meta>","Saga",0.5f);

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
