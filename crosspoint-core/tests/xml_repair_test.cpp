// SPDX-License-Identifier: MIT
// XmlRepairPipeline: entity and element repair must leave well-formed XML
// unchanged, fix the common breakage exactly as specified, and always give
// expat a document it accepts, whatever the read boundaries are.
#include "Epub/XmlRepairPipeline.h"

#include <expat.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace {

// Runs the chunks through the pipeline and returns everything it emitted.
std::string repairChunks(const std::vector<std::string>& chunks) {
  auto pipeline = std::make_unique<XmlRepairPipeline>();
  std::string out;
  for (size_t c = 0; c < chunks.size(); ++c) {
    const bool last = c + 1 == chunks.size();
    const bool ok = pipeline->push(chunks[c].data(), chunks[c].size(), last,
                                   [&out](const char* d, size_t n) {
                                     out.append(d, n);
                                     return true;
                                   });
    assert(ok);
  }
  return out;
}

std::string repairOnce(const std::string& input) { return repairChunks({input}); }

bool expatAccepts(const std::string& doc) {
  XML_Parser parser = XML_ParserCreateNS(nullptr, '|');
  const bool ok = XML_Parse(parser, doc.data(), static_cast<int>(doc.size()), XML_TRUE) == XML_STATUS_OK;
  if (!ok) {
    std::fprintf(stderr, "expat rejected: %s\n  %s\n", XML_ErrorString(XML_GetErrorCode(parser)), doc.c_str());
  }
  XML_ParserFree(parser);
  return ok;
}

void expectEq(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "%s: got [%s] want [%s]\n", what, got.c_str(), want.c_str());
    assert(false);
  }
}

// Small deterministic generator so failures reproduce.
struct Lcg {
  uint64_t state;
  uint32_t next() {
    state = state * 6364136223846793005ULL + 1442695040888963407ULL;
    return static_cast<uint32_t>(state >> 33);
  }
  size_t below(size_t n) { return next() % n; }
};

}  // namespace

int main() {
  // Well-formed markup of every kind comes out byte for byte the same.
  const std::string wellFormed =
      "<?xml version=\"1.0\"?>\n<!DOCTYPE html>\n<html><head><title>T</title></head>"
      "<body class=\"a>b\"><p>x &amp; y &lt; z</p><br/><img src='i.png' alt=\"q\"/>"
      "<!-- c - d --><![CDATA[<raw>]]><?pi data?><p>\xC3\xA9 \xE6\xBC\xA2</p></body></html>\n";
  expectEq(repairOnce(wellFormed), wellFormed, "well-formed identity");
  assert(expatAccepts(wellFormed));
  for (size_t p = 0; p <= wellFormed.size(); ++p) {
    expectEq(repairChunks({wellFormed.substr(0, p), wellFormed.substr(p)}), wellFormed, "identity split");
  }

  // Exact repairs for the common breakage.
  expectEq(repairOnce("<p>a<b>c</p>d"), "<p>a<b>c</b></p>d", "close parent closes child");
  expectEq(repairOnce("<body><br>x</body>"), "<body><br></br>x</body>", "void tag closed");
  expectEq(repairOnce("a < b"), "a &lt; b", "bare less-than is text");
  expectEq(repairOnce("<a><b>x"), "<a><b>x</b></a>", "open at end closed");
  expectEq(repairOnce("</zzz>text"), "text", "stray end tag dropped");
  expectEq(repairOnce("<body><p>one<b>bold<i>italic<p class='next'>two</body>"),
           "<body><p>one<b>bold<i>italic</i></b></p><p class='next'>two</p></body>",
           "new paragraph closes the old paragraph and its formatting");
  expectEq(repairOnce("<body><p><em>one<hr>two</body>"),
           "<body><p><em>one</em></p><hr></hr>two</body>", "a void block closes the paragraph");
  const std::string nestedLink = "<body><a href='#x'><div><p>linked</p></div></a></body>";
  expectEq(repairOnce(nestedLink), nestedLink, "valid link wrapping a block is preserved");
  expectEq(repairOnce("<a href='x>y'>t</a>"), "<a href='x>y'>t</a>", "'>' inside a quoted attribute");

  // Read boundaries never change the result.
  const std::string broken = "<body><p>a<b>c<div>d<br>e & f<!-- x -->&nbsp;</body>";
  const std::string brokenWhole = repairOnce(broken);
  for (size_t p = 0; p <= broken.size(); ++p) {
    expectEq(repairChunks({broken.substr(0, p), broken.substr(p)}), brokenWhole, "broken split");
  }
  std::vector<std::string> oneByte;
  for (char c : broken) oneByte.emplace_back(1, c);
  expectEq(repairChunks(oneByte), brokenWhole, "broken one byte per read");

  // Random malformed documents, cut at random points: expat must accept every
  // repaired result. Tokens cover tags that break nesting, comments, CDATA,
  // stray ampersands and '<'.
  static const char* const kTokens[] = {
      "<p>", "</p>", "<b>", "</b>", "<i>", "</i>", "<br>", "<br/>", "<img src='x'>",
      "</div>", "<div class=\"a>b\">", "</div >", "text", " & ", "&nbsp;", "&amp;", "< ",
      "<!-- c -->", "<![CDATA[ x<y ]]>", "<?pi x?>", "</body>", "</html>", "<h1>", "</h1>",
      "</zzz>", "<a title=\"x\">", "</a>", "\xC3\xA9", "<span>", "</span>",
      ">", "'", "\"", "<ul><li>", "</li></ul>", "<hr>", "</ hr>", "<sub>", "</sub>",
  };
  constexpr size_t kTokenCount = sizeof(kTokens) / sizeof(kTokens[0]);
  Lcg rng{20260522};
  size_t documents = 0;
  for (int round = 0; round < 1500; ++round) {
    std::string body;
    const size_t tokens = 1 + rng.below(40);
    for (size_t t = 0; t < tokens; ++t) body += kTokens[rng.below(kTokenCount)];
    const std::string doc = "<root>" + body + "</root>";
    // The document ends inside the root, so the repaired output must be
    // a complete document to expat.
    std::vector<std::string> chunks;
    size_t pos = 0;
    while (pos < doc.size()) {
      const size_t n = 1 + rng.below(9);
      chunks.push_back(doc.substr(pos, n));
      pos += n;
    }
    const std::string repaired = repairChunks(chunks);
    assert(expatAccepts(repaired));
    ++documents;
  }
  std::printf("Books XML repair: %zu random malformed documents accepted by expat\n", documents);

  // Output never exceeds the stated per-call room; past the depth limit,
  // elements are not tracked, so only the tracked ones are closed.
  auto pipeline = std::make_unique<XmlRepairPipeline>();
  std::string deep;
  for (int k = 0; k < 200; ++k) deep += "<x>";
  for (int k = 0; k < 200; ++k) deep += "</y>";
  size_t total = 0;
  assert(pipeline->push(deep.data(), deep.size(), true, [&total](const char*, size_t n) {
    assert(n <= XmlRepairPipeline::kPieceOut);
    total += n;
    return true;
  }));
  // 200 start tags pass through; only the first 64 fit the stack and are closed.
  assert(total == 200 * 3 + 64 * 4);

  std::puts("Books XML tag repair: OK");
  return 0;
}
