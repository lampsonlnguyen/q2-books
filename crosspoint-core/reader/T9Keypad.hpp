// SPDX-License-Identifier: MIT
#pragma once
// The search keyboard, laid out as the Q2's own T9 keyboard (Shanling's
// stock kb_default_t9, an AWTK 4x4 keypad, as Q2 Pod shows it: its EN key
// is a case key). Letters are typed multi-tap: pressing the same key again
// within kT9RepeatMs replaces the letter just typed with the key's next
// one; another key, or a pause, keeps it. No timer is needed: the letter is
// in the text from the first press.
//
//   ,      abc    def    DEL
//   ghi    jkl    mno    ABC   (case: abc on the capitals)
//   pqrs   tuv    wxyz   123   (digits: each key types its number)
//   .?!    space  Done   CLR
//
// For a URL, a user name or a password (setUrlMode), the two punctuation
// keys add what addresses need: ",'-:;@_" and "./?!=&%~#".
#include <chrono>
#include <cstddef>
#include <string>

namespace books {

inline constexpr int kT9Keys = 16;
inline constexpr int kT9RepeatMs = 1200;

enum class T9Result { None, Typed, Done };

class T9Keypad {
 public:
  using Clock = std::chrono::steady_clock;
  enum Key { Comma, Abc, Def, Delete, Ghi, Jkl, Mno, Case, Pqrs, Tuv, Wxyz, Digits, Stops, Space, Done, Clear };

  bool upper() const { return upper_; }
  bool digits() const { return digits_; }
  bool urlMode() const { return url_; }
  void setUrlMode(bool on) { url_ = on; forget(); }
  void reset() { upper_ = digits_ = false; last_ = -1; tap_ = 0; }

  // The characters a key cycles through (empty for function keys).
  std::string letters(int key) const {
    static const char* const lower[kT9Keys] = {",'-:;", "abc", "def", "", "ghi", "jkl", "mno", "",
                                               "pqrs", "tuv", "wxyz", "", ".?!", " ", "", ""};
    static const char digit[kT9Keys] = {'1', '2', '3', 0, '4', '5', '6', 0, '7', '8', '9', 0, '0', ' ', 0, 0};
    if (key < 0 || key >= kT9Keys) return {};
    if (digits_) return digit[key] ? std::string(1, digit[key]) : std::string();
    std::string out = lower[key];
    if (url_ && key == Comma) out = ",'-:;@_";
    if (url_ && key == Stops) out = "./?!=&%~#";
    if (upper_)
      for (char& c : out)
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 32);
    return out;
  }
  // The key's label on the keypad.
  std::string label(int key) const {
    switch (key) {
      case Delete: return "DEL";
      case Case: return upper_ ? "abc" : "ABC";
      case Digits: return digits_ ? "abc" : "123";
      case Space: return "space";
      case Done: return "Done";
      case Clear: return "CLR";
      case Comma: return digits_ ? "1" : url_ ? ",:@_" : ",";
      case Stops: return digits_ ? "0" : url_ ? "./?=&" : letters(key);
      default: return letters(key);
    }
  }
  static bool functionKey(int key) {
    return key == Delete || key == Case || key == Digits || key == Done || key == Clear;
  }

  // Press |key|, editing |text| (at most |maxLen| bytes, ASCII only).
  T9Result press(int key, std::string& text, size_t maxLen, Clock::time_point now = Clock::now()) {
    if (key < 0 || key >= kT9Keys) return T9Result::None;
    const bool repeat = key == last_ && now - lastAt_ <= std::chrono::milliseconds(kT9RepeatMs);
    switch (key) {
      case Delete:
        if (!text.empty()) {
          size_t start = text.size() - 1;
          while (start && (static_cast<unsigned char>(text[start]) & 0xc0) == 0x80) --start;
          text.erase(start);
        }
        forget();
        return T9Result::Typed;
      case Clear:
        text.clear();
        forget();
        return T9Result::Typed;
      case Case:
        upper_ = !upper_;
        digits_ = false;
        forget();
        return T9Result::None;
      case Digits:
        digits_ = !digits_;
        forget();
        return T9Result::None;
      case Done:
        forget();
        return T9Result::Done;
      default: break;
    }
    const std::string set = letters(key);
    if (set.empty()) return T9Result::None;
    if (repeat && set.size() > 1 && !text.empty()) {
      tap_ = (tap_ + 1) % static_cast<int>(set.size());
      text.back() = set[static_cast<size_t>(tap_)];
    } else {
      if (text.size() >= maxLen) return T9Result::None;
      tap_ = 0;
      text.push_back(set[0]);
    }
    last_ = key;
    lastAt_ = now;
    return T9Result::Typed;
  }
  // While a multi-tap is open: the key whose letters show above the keypad
  // and which of them is in the text. -1 when none is.
  int pendingKey(Clock::time_point now = Clock::now()) const {
    if (last_ < 0 || now - lastAt_ > std::chrono::milliseconds(kT9RepeatMs)) return -1;
    return letters(last_).size() > 1 ? last_ : -1;
  }
  int pendingIndex() const { return tap_; }

 private:
  void forget() { last_ = -1; tap_ = 0; }
  bool upper_ = false, digits_ = false, url_ = false;
  int last_ = -1, tap_ = 0;
  Clock::time_point lastAt_{};
};

}  // namespace books
