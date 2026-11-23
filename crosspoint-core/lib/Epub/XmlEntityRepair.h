#pragma once
// Makes stray HTML entities and bare ampersands well-formed before expat sees
// them. Real EPUBs often contain "&nbsp;", "&copy;" or a plain "&", which XML
// rejects; expat then fails the whole chapter. This filter rewrites them:
//   &amp; &lt; &gt; &quot; &apos; and numeric references pass through,
//   a known HTML name (lookupHtmlEntity) becomes its UTF-8 text,
//   any other "&name;" and a bare "&" become "&amp;" so the text is shown.
// Tags are not touched; unclosed or mismatched tags remain a parse error.
// Bytes that could be the start of an entity split across two reads are kept
// until the next call (or flushed when `last` is true).

#include <cstddef>
#include <cstring>

#include "Epub/htmlEntities.h"

class XmlEntityRepair {
 public:
  // Longest entity name (between '&' and ';') that is waited for or repaired.
  static constexpr size_t kMaxName = 31;

  // Output needed for `inputLen` new bytes: a bare '&' expands to 5 bytes, and
  // up to kMaxName+1 bytes may be carried over from the previous call.
  static constexpr size_t maxOutput(size_t inputLen) { return 5 * (inputLen + kMaxName + 1); }

  // Repairs in[0..len) (after any carried-over bytes) into out. out must hold
  // maxOutput(len) bytes. Returns the number of bytes written.
  size_t process(const char* in, size_t len, bool last, char* out) {
    const size_t total = pendingLen_ + len;
    size_t o = 0;
    size_t i = 0;
    while (i < total) {
      const char c = at(in, len, i);
      if (c != '&') {
        out[o++] = c;
        ++i;
        continue;
      }
      // Scan the candidate name after '&'. A '#' may only open a numeric one.
      size_t j = i + 1;
      while (j < total && j - i <= kMaxName && isNameByte(at(in, len, j), j == i + 1)) ++j;
      if (j == total && !last && j - i <= kMaxName) {
        // The name may continue or end in the next read: keep it for later.
        stash(in, len, i, total);
        return o;
      }
      if (j < total && j > i + 1 && j - i <= kMaxName + 1 && at(in, len, j) == ';') {
        char name[kMaxName + 2];
        size_t n = 0;
        for (size_t k = i + 1; k < j; ++k) name[n++] = at(in, len, k);
        name[n] = '\0';
        if (writeKnownEntity(name, n, out, o)) {
          i = j + 1;
          continue;
        }
      }
      // Not a usable reference: show the ampersand as text.
      static constexpr char kAmp[] = "&amp;";
      std::memcpy(out + o, kAmp, 5);
      o += 5;
      ++i;
    }
    pendingLen_ = 0;
    return o;
  }

 private:
  static bool isNameByte(char c, bool first) {
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return true;
    if (c == '_' || c == '-' || c == '.' || c == ':') return true;
    return c == '#' && first;
  }

  // Writes the replacement for the reference `name` (without & and ;) and
  // returns true, or returns false when it is not a reference we can repair.
  static bool writeKnownEntity(const char* name, size_t n, char* out, size_t& o) {
    static constexpr const char* kXml[] = {"amp", "lt", "gt", "quot", "apos"};
    for (const char* x : kXml) {
      if (std::strlen(x) == n && std::memcmp(x, name, n) == 0) {
        out[o++] = '&';
        std::memcpy(out + o, name, n);
        o += n;
        out[o++] = ';';
        return true;
      }
    }
    if (name[0] == '#') {
      // Numeric references are valid as long as digits follow (&#160; &#xA0;).
      const bool hex = n > 1 && (name[1] == 'x');
      const size_t start = hex ? 2 : 1;
      if (n <= start) return false;
      for (size_t k = start; k < n; ++k) {
        const char d = name[k];
        const bool ok = (d >= '0' && d <= '9') || (hex && ((d >= 'a' && d <= 'f') || (d >= 'A' && d <= 'F')));
        if (!ok) return false;
      }
      out[o++] = '&';
      std::memcpy(out + o, name, n);
      o += n;
      out[o++] = ';';
      return true;
    }
    // Named HTML entity: the table key includes '&' and ';'.
    char key[kMaxName + 4];
    key[0] = '&';
    std::memcpy(key + 1, name, n);
    key[n + 1] = ';';
    const char* utf8 = lookupHtmlEntity(key, n + 2);
    if (!utf8) return false;
    const size_t len = std::strlen(utf8);
    std::memcpy(out + o, utf8, len);
    o += len;
    return true;
  }

  char at(const char* in, size_t len, size_t k) const {
    (void)len;
    return k < pendingLen_ ? pending_[k] : in[k - pendingLen_];
  }

  void stash(const char* in, size_t len, size_t from, size_t to) {
    char tmp[kMaxName + 2];
    const size_t n = to - from;
    for (size_t k = 0; k < n; ++k) tmp[k] = at(in, len, from + k);
    std::memcpy(pending_, tmp, n);
    pendingLen_ = n;
  }

  char pending_[kMaxName + 2] = {};
  size_t pendingLen_ = 0;
};
