// SPDX-License-Identifier: MIT
// XmlEntityRepair: stray HTML entities and bare ampersands become well-formed
// XML, whatever the read boundaries are.
#include "Epub/XmlEntityRepair.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

// Feeds the chunks one at a time, as the parser does with 1024-byte reads.
std::string repairChunks(const std::vector<std::string>& chunks) {
  XmlEntityRepair repair;
  std::string out;
  std::vector<char> buffer;
  for (size_t c = 0; c < chunks.size(); ++c) {
    const bool last = c + 1 == chunks.size();
    buffer.assign(XmlEntityRepair::maxOutput(chunks[c].size()) + 1, '\0');
    const size_t n = repair.process(chunks[c].data(), chunks[c].size(), last, buffer.data());
    out.append(buffer.data(), n);
  }
  return out;
}

std::string repairOnce(const std::string& input) { return repairChunks({input}); }

void expectEq(const std::string& got, const std::string& want, const char* what) {
  if (got != want) {
    std::fprintf(stderr, "%s: got [%s] want [%s]\n", what, got.c_str(), want.c_str());
    assert(false);
  }
}

}  // namespace

int main() {
  // Bare ampersands and unknown references are shown as text.
  expectEq(repairOnce("a & b"), "a &amp; b", "bare ampersand");
  expectEq(repairOnce("&undefined; text"), "&amp;undefined; text", "unknown name");
  expectEq(repairOnce("&#xZZ; &#; &"), "&amp;#xZZ; &amp;#; &amp;", "invalid numeric and trailing ampersand");

  // Named HTML entities become their characters.
  expectEq(repairOnce("x&nbsp;y"), "x\xC2\xA0y", "nbsp");
  expectEq(repairOnce("&copy; &mdash; &hellip;"), "\xC2\xA9 \xE2\x80\x94 \xE2\x80\xA6", "copy mdash hellip");

  // References XML already allows pass through unchanged.
  expectEq(repairOnce("&amp; &lt; &gt; &quot; &apos;"), "&amp; &lt; &gt; &quot; &apos;", "xml predefined");
  expectEq(repairOnce("&#160; &#xA0; &#x3B1;"), "&#160; &#xA0; &#x3B1;", "numeric references");

  // Tags and ordinary UTF-8 text are untouched.
  expectEq(repairOnce("<p class=\"a\">h\xC3\xA9llo \xE6\xBC\xA2</p>"),
           "<p class=\"a\">h\xC3\xA9llo \xE6\xBC\xA2</p>", "tags and utf-8");

  // A reference split across any read boundary repairs the same way as one
  // read. Every two-chunk split of a mixed sample is checked.
  const std::string sample = "A &nbsp;B &amp; C &undefined; D &#xA0; E & F &copy; G";
  const std::string whole = repairOnce(sample);
  for (size_t p = 0; p <= sample.size(); ++p) {
    expectEq(repairChunks({sample.substr(0, p), sample.substr(p)}), whole, "split point");
  }

  // Several small reads, one byte each.
  std::vector<std::string> bytes;
  for (char c : sample) bytes.emplace_back(1, c);
  expectEq(repairChunks(bytes), whole, "one byte per read");

  // Unfinished reference at the end of the book is flushed as text.
  expectEq(repairChunks({"a &nbs"}), "a &amp;nbs", "unterminated at end");
  expectEq(repairChunks({"x &nb", "sp;"}), "x \xC2\xA0", "reference split over reads");

  // A name longer than the limit is not a reference.
  const std::string longName(XmlEntityRepair::kMaxName + 5, 'a');
  expectEq(repairOnce("&" + longName + ";"), "&amp;" + longName + ";", "over-long name");

  // Output never exceeds the reserved size, even for ampersand-heavy input.
  const std::string heavy(1024, '&');
  XmlEntityRepair repair;
  std::vector<char> out(XmlEntityRepair::maxOutput(heavy.size()));
  const size_t n = repair.process(heavy.data(), heavy.size(), true, out.data());
  assert(n == heavy.size() * 5);

  std::puts("Books XML entity repair: OK");
  return 0;
}
