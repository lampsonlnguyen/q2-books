#pragma once
// Makes element nesting well-formed for expat, one byte stream at a time.
// Real EPUBs often leave tags open ("<b>" with no "</b>"), close a parent
// before a child ("</p>" while "<b>" is open), use HTML void tags ("<br>"),
// or contain a stray end tag. Expat stops at the first such error and the
// whole chapter is lost. This stage keeps a stack of open elements and:
//   - passes start tags, text, comments, CDATA and processing instructions
//     through unchanged;
//   - writes "</name>" for elements left open above a matching end tag, and
//     drops an end tag that matches nothing open;
//   - writes "</br>" right after a void start tag that is not self-closing;
//   - closes an open paragraph and its inline children before a new block;
//   - closes everything still open at the end of the chapter.
// Ordinary well-formed XHTML comes out byte for byte the same. Output is produced in
// steps, so a caller with a fixed buffer calls feed() until idle().

#include <cstddef>
#include <cstring>

class XmlTagRepair {
 public:
  static constexpr size_t kMaxDepth = 64;
  static constexpr size_t kMaxName = 32;
  static constexpr size_t kMaxEndTag = 48;
  // Room needed before one step: the longest single write is a buffered end
  // tag or a closing tag.
  static constexpr size_t kMaxEmit = kMaxEndTag + kMaxName;
  // A caller should offer at least this much output room per feed() call.
  static constexpr size_t kMinCapacity = 2 * kMaxEmit;

  // Consumes input bytes into out. Sets *used to the number consumed (the
  // caller passes in+*used next time). Returns the bytes written. Pass
  // last=true on the final input; the call then closes every open element.
  size_t feed(const char* in, size_t len, bool last, char* out, size_t cap, size_t* used) {
    size_t o = 0;
    size_t i = 0;
    for (;;) {
      if (cap - o < kMaxEmit) break;
      if (closes_ > 0) {
        --depth_;
        writeCloseTag(out, o, stack_[depth_], stackLen_[depth_]);
        --closes_;
        continue;
      }
      if (voidClose_) {
        writeCloseTag(out, o, voidName_, voidLen_);
        voidClose_ = false;
        continue;
      }
      if (endReady_) {
        std::memcpy(out + o, endBuf_, endLen_);
        o += endLen_;
        --depth_;
        endReady_ = false;
        continue;
      }
      if (i >= len) {
        if (last && !flushed_) {
          flushAtEnd(out, o);
          flushed_ = true;
          closes_ = depth_;
          continue;
        }
        break;
      }
      step(in[i], out, o, i);
    }
    *used = i;
    return o;
  }

  // True when nothing is pending: all input is consumed (the caller checks
  // that) and no close or end tag is waiting to be written.
  bool idle(bool last) const {
    return closes_ == 0 && !voidClose_ && !endReady_ && (!last || flushed_);
  }

 private:
  enum class State : unsigned char { Text, Lt, StartName, StartPrefix, StartTag, EndTag, SkipEnd, Decl, Raw };

  static bool isNameStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':' ||
           static_cast<unsigned char>(c) >= 0x80;
  }
  static bool isNameByte(char c) {
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
  }
  template <size_t N>
  static bool nameIn(const char* n, size_t len, const char* const (&names)[N]) {
    for (const char* candidate : names) {
      if (std::strlen(candidate) == len && std::memcmp(candidate, n, len) == 0) return true;
    }
    return false;
  }
  static bool isVoid(const char* n, size_t len) {
    static constexpr const char* kVoid[] = {"area", "base", "br", "col", "embed", "hr", "img",
                                            "input", "link", "meta", "param", "source", "track", "wbr"};
    return nameIn(n, len, kVoid);
  }

  void closeParagraphBeforeBlock() {
    static constexpr const char* kBlocks[] = {
        "address", "article", "aside", "blockquote", "details", "dialog", "div", "dl", "fieldset",
        "figcaption", "figure", "footer", "form", "h1", "h2", "h3", "h4", "h5", "h6", "header",
        "hgroup", "hr", "main", "menu", "nav", "ol", "p", "pre", "search", "section", "table", "ul"};
    static constexpr const char* kInline[] = {
        "a", "abbr", "b", "bdi", "bdo", "cite", "code", "del", "dfn", "em", "font", "i", "ins",
        "kbd", "mark", "q", "rp", "rt", "ruby", "s", "samp", "small", "span", "strike", "strong",
        "sub", "sup", "time", "tt", "u", "var"};
    if (depth_ < 2 || !nameIn(name_, nameLen_, kBlocks)) return;
    for (size_t j = depth_; j-- > 1;) {
      if (stackLen_[j] == 1 && stack_[j][0] == 'p') {
        closes_ = depth_ - j;
        return;
      }
      if (!nameIn(stack_[j], stackLen_[j], kInline)) return;
    }
  }

  void writeStartPrefix(char* out, size_t& o) const {
    out[o++] = '<';
    std::memcpy(out + o, name_, nameLen_);
    o += nameLen_;
  }
  static bool prefixOf(const char* s, size_t n, const char* p) {
    const size_t pl = std::strlen(p);
    return n <= pl && std::memcmp(s, p, n) == 0;
  }
  static void writeCloseTag(char* out, size_t& o, const char* name, size_t len) {
    out[o++] = '<';
    out[o++] = '/';
    std::memcpy(out + o, name, len);
    o += len;
    out[o++] = '>';
  }

  void setRaw(const char* term) {
    termLen_ = std::strlen(term);
    std::memcpy(term_, term, termLen_);
    tailLen_ = 0;
    state_ = State::Raw;
  }

  void finishStart(bool selfClosing) {
    if (selfClosing || nameOverflow_) return;
    if (isVoid(name_, nameLen_)) {
      voidClose_ = true;
      std::memcpy(voidName_, name_, nameLen_);
      voidLen_ = nameLen_;
      return;
    }
    if (depth_ < kMaxDepth) {
      std::memcpy(stack_[depth_], name_, nameLen_);
      stackLen_[depth_] = static_cast<unsigned char>(nameLen_);
      ++depth_;
    }
  }

  // Decides what an end tag (already in endBuf_, ending in '>') does.
  void resolveEnd() {
    size_t nl = 0;
    while (2 + nl < endLen_ && isNameByte(endBuf_[2 + nl])) ++nl;
    for (size_t j = depth_; j-- > 0;) {
      if (stackLen_[j] == nl && std::memcmp(stack_[j], endBuf_ + 2, nl) == 0) {
        closes_ = depth_ - 1 - j;
        endReady_ = true;
        return;
      }
    }
    // Matches nothing open: dropped.
  }

  void flushAtEnd(char* out, size_t& o) {
    if (state_ == State::Lt) {
      std::memcpy(out + o, "&lt;", 4);
      o += 4;
      state_ = State::Text;
    } else if (state_ == State::StartName || state_ == State::StartPrefix) {
      writeStartPrefix(out, o);
      state_ = State::Text;
    } else if (state_ == State::Decl) {
      out[o++] = '<';
      out[o++] = '!';
      std::memcpy(out + o, decl_, declLen_);
      o += declLen_;
      state_ = State::Text;
    }
  }

  // Processes one input byte (or re-processes it in a new state when the
  // state does not consume it: see the Lt and StartName cases).
  void step(char c, char* out, size_t& o, size_t& i) {
    switch (state_) {
      case State::Text:
        if (c == '<') {
          state_ = State::Lt;
        } else {
          out[o++] = c;
        }
        ++i;
        return;
      case State::Lt:
        if (isNameStart(c)) {
          name_[0] = c;
          nameLen_ = 1;
          nameOverflow_ = false;
          state_ = State::StartName;
          ++i;
        } else if (c == '/') {
          endBuf_[0] = '<';
          endBuf_[1] = '/';
          endLen_ = 2;
          state_ = State::EndTag;
          ++i;
        } else if (c == '!') {
          declLen_ = 0;
          state_ = State::Decl;
          ++i;
        } else if (c == '?') {
          out[o++] = '<';
          out[o++] = '?';
          setRaw("?>");
          ++i;
        } else {
          // A '<' that starts no markup is text. Do not consume c.
          std::memcpy(out + o, "&lt;", 4);
          o += 4;
          state_ = State::Text;
        }
        return;
      case State::StartName:
        if (isNameByte(c) && nameLen_ < kMaxName) {
          name_[nameLen_++] = c;
          ++i;
        } else {
          nameOverflow_ = isNameByte(c);
          if (!nameOverflow_) closeParagraphBeforeBlock();
          state_ = State::StartPrefix;
        }
        return;
      case State::StartPrefix:
        // feed() emits any implied closes before the new start tag.
        writeStartPrefix(out, o);
        state_ = State::StartTag;
        prev_ = 0;
        quote_ = 0;
        return;
      case State::StartTag:
        if (quote_) {
          if (c == quote_) quote_ = 0;
          out[o++] = c;
          prev_ = c;
          ++i;
        } else if (c == '"' || c == '\'') {
          quote_ = c;
          out[o++] = c;
          prev_ = c;
          ++i;
        } else if (c == '>') {
          out[o++] = '>';
          ++i;
          finishStart(prev_ == '/');
          state_ = State::Text;
        } else {
          out[o++] = c;
          prev_ = c;
          ++i;
        }
        return;
      case State::EndTag:
        if (c == '>') {
          endBuf_[endLen_++] = '>';
          ++i;
          resolveEnd();
          state_ = State::Text;
        } else if (endLen_ + 2 < kMaxEndTag) {
          endBuf_[endLen_++] = c;
          ++i;
        } else {
          state_ = State::SkipEnd;
          ++i;
        }
        return;
      case State::SkipEnd:
        if (c == '>') state_ = State::Text;
        ++i;
        return;
      case State::Decl:
        if (c == '>') {
          // "<!>" or "<!word>": pass it through without markup meaning.
          out[o++] = '<';
          out[o++] = '!';
          std::memcpy(out + o, decl_, declLen_);
          o += declLen_;
          out[o++] = '>';
          state_ = State::Text;
          ++i;
          return;
        }
        decl_[declLen_++] = c;
        ++i;
        {
          const bool comment = prefixOf(decl_, declLen_, "--");
          const bool cdata = prefixOf(decl_, declLen_, "[CDATA[");
          if (!comment && !cdata) {
            out[o++] = '<';
            out[o++] = '!';
            std::memcpy(out + o, decl_, declLen_);
            o += declLen_;
            setRaw(">");
          } else if (comment && declLen_ == 2) {
            std::memcpy(out + o, "<!--", 4);
            o += 4;
            setRaw("-->");
          } else if (cdata && declLen_ == 7) {
            std::memcpy(out + o, "<![CDATA[", 9);
            o += 9;
            setRaw("]]>");
          }
        }
        return;
      case State::Raw:
        out[o++] = c;
        ++i;
        if (tailLen_ < 3) {
          tail_[tailLen_++] = c;
        } else {
          tail_[0] = tail_[1];
          tail_[1] = tail_[2];
          tail_[2] = c;
        }
        if (tailLen_ >= termLen_ && std::memcmp(tail_ + tailLen_ - termLen_, term_, termLen_) == 0) {
          state_ = State::Text;
        }
        return;
    }
  }

  State state_ = State::Text;
  char name_[kMaxName] = {};
  size_t nameLen_ = 0;
  bool nameOverflow_ = false;
  char quote_ = 0;
  char prev_ = 0;
  char endBuf_[kMaxEndTag] = {};
  size_t endLen_ = 0;
  char decl_[8] = {};
  size_t declLen_ = 0;
  char term_[4] = {};
  size_t termLen_ = 0;
  char tail_[3] = {};
  size_t tailLen_ = 0;
  char stack_[kMaxDepth][kMaxName] = {};
  unsigned char stackLen_[kMaxDepth] = {};
  size_t depth_ = 0;
  size_t closes_ = 0;
  bool voidClose_ = false;
  char voidName_[kMaxName] = {};
  size_t voidLen_ = 0;
  bool endReady_ = false;
  bool flushed_ = false;
};
