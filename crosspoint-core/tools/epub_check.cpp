// Books EPUB validation utility: real CrossPoint ZIP and container parser.
// This is an engine diagnostic, NOT the Q2 reading user interface.
#include <ContainerParser.h>
#include <ZipFile.h>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: books-epub-check <book.epub>\n";
    return 2;
  }
  const std::string path = argv[1];
  ZipFile archive(path);
  size_t mimeSize = 0;
  if (!archive.getInflatedFileSize("mimetype", &mimeSize) || mimeSize != 20) {
    std::cerr << "Invalid EPUB: missing or unexpected mimetype\n";
    return 1;
  }
  auto* mime = archive.readFileToMemory("mimetype", nullptr, false);
  if (!mime || std::memcmp(mime, "application/epub+zip", mimeSize) != 0) {
    std::free(mime);
    std::cerr << "Invalid EPUB mimetype\n";
    return 1;
  }
  std::free(mime);
  size_t xmlSize = 0;
  if (!archive.getInflatedFileSize("META-INF/container.xml", &xmlSize)
      || xmlSize == 0 || xmlSize > (1u << 20)) {
    std::cerr << "Invalid EPUB container.xml (limit 1 MiB)\n";
    return 1;
  }
  ContainerParser container(xmlSize);
  if (!container.setup() ||
      !archive.readFileToStream("META-INF/container.xml", container, 1024) ||
      container.fullPath.empty()) {
    std::cerr << "Unable to parse EPUB container.xml\n";
    return 1;
  }
  const std::string& opf = container.fullPath;
  if (opf.front() == '/' || opf.find("..") != std::string::npos ||
      opf.find('\\') != std::string::npos) {
    std::cerr << "Unsafe OPF path\n";
    return 1;
  }
  size_t packageSize = 0;
  if (!archive.getInflatedFileSize(opf.c_str(), &packageSize) ||
      packageSize == 0 || packageSize > (4u << 20)) {
    std::cerr << "EPUB package missing or exceeds 4 MiB limit\n";
    return 1;
  }
  unsigned entries = 0;
  if (!archive.enumerateFilePaths([&entries](std::string_view) { ++entries; })) {
    std::cerr << "Unable to enumerate ZIP entries\n";
    return 1;
  }
  std::cout << "CrossPoint ZIP/EPUB container: OK\n";
  std::cout << "OPF: " << opf << "\n";
  std::cout << "OPF bytes: " << packageSize << "\n";
  std::cout << "ZIP entries: " << entries << "\n";
  std::cout << "Rendering engine: NOT YET CONNECTED\n";
  return 0;
}
