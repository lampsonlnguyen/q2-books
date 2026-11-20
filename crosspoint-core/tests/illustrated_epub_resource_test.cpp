// Alpha 2 image ingestion regression: a real EPUB ZIP and its PNG resource.
// This tests safe discovery/extraction, NOT image decoding or page painting.
#include <ZipFile.h>
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>

static uint32_t bigEndian32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}
int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string path(argv[1]);  // ZipFile borrows this string.
  ZipFile zip(path);
  assert(zip.loadAllFileStatSlims());
  size_t chapterSize = 0;
  uint8_t* chapter = zip.readFileToMemory("OEBPS/text/chapter.xhtml",
                                            &chapterSize, true);
  assert(chapter && chapterSize > 0);
  const std::string html(reinterpret_cast<const char*>(chapter), chapterSize);
  std::free(chapter);
  // Verify the EPUB image href's relative base and manifest resource.
  assert(html.find("src=\"../images/test.png\"") != std::string::npos);
  size_t imageSize = 0;
  const char* imageEntry = "OEBPS/images/test.png";
  assert(zip.getInflatedFileSize(imageEntry, &imageSize));
  assert(imageSize >= 33 && imageSize <= 1024 * 1024);
  uint8_t* data = zip.readFileToMemory(imageEntry, &imageSize, false);
  assert(data);
  static const uint8_t signature[] = {137, 80, 78, 71, 13, 10, 26, 10};
  assert(std::memcmp(data, signature, sizeof(signature)) == 0);
  assert(std::memcmp(data + 12, "IHDR", 4) == 0);
  assert(bigEndian32(data + 16) == 2);
  assert(bigEndian32(data + 20) == 2);
  assert(data[24] == 8 && data[25] == 2);
  std::free(data);
  size_t missing = 0;
  assert(!zip.getInflatedFileSize("OEBPS/images/missing.png", &missing));
  return 0;
}
