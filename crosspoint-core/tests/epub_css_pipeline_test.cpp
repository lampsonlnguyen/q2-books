#include <CssParser.h>
#include <HalStorage.h>
#include <Print.h>
#include <ZipFile.h>
#include <cassert>
#include <cstdio>
#include <string>

class HalFileSink final : public Print {
 public:
  explicit HalFileSink(HalFile& file) : file_(file) {}
  size_t write(uint8_t byte) override { return file_.write(&byte, 1); }
  size_t write(const uint8_t* data, size_t count) override {
    return file_.write(data, count);
  }
 private:
  HalFile& file_;
};

int main(int argc, char** argv) {
  assert(argc == 2);
  const std::string path(argv[1]);
  ZipFile book(path);
  // Streaming extraction does not inflate the whole CSS entry into memory.
  HalFile cssFile(std::tmpfile());
  assert(cssFile.isOpen());
  HalFileSink sink(cssFile);
  assert(book.readFileToStream("OEBPS/book.css", sink, 13));
  assert(cssFile.seek(0));
  CssParser stylesheet("");
  assert(stylesheet.loadFromStream(cssFile) == CssParser::ParseResult::Complete);
  assert(stylesheet.ruleCount() >= 2);
  const auto plain = stylesheet.resolveStyle("p", "");
  const auto emphasized = stylesheet.resolveStyle("p", "highlight");
  assert(plain.textAlign == CssTextAlign::Left);
  assert(emphasized.textAlign == CssTextAlign::Center);
  assert(emphasized.fontStyle == CssFontStyle::Italic);
  return 0;
}
