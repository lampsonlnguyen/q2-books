// The T9 search keypad: multi-tap cycling and its timeout, case and digit
// pages, delete/clear/done, and the length limit.
#include <cassert>
#include <chrono>
#include <cstdio>
#include <string>

#include "T9Keypad.hpp"

using books::T9Keypad;
using books::T9Result;
using namespace std::chrono_literals;

int main() {
  T9Keypad k;
  std::string t;
  auto now = T9Keypad::Clock::time_point{} + 10s;
  // "cab": c = abc x3; then a after a pause; b = abc x2 quickly.
  assert(k.press(T9Keypad::Abc, t, 24, now) == T9Result::Typed && t == "a");
  assert(k.press(T9Keypad::Abc, t, 24, now + 300ms) == T9Result::Typed && t == "b");
  assert(k.press(T9Keypad::Abc, t, 24, now + 600ms) == T9Result::Typed && t == "c");
  assert(k.pendingKey(now + 700ms) == T9Keypad::Abc && k.pendingIndex() == 2);
  assert(k.press(T9Keypad::Abc, t, 24, now + 3s) == T9Result::Typed && t == "ca");  // pause: new letter
  assert(k.press(T9Keypad::Abc, t, 24, now + 3200ms) == T9Result::Typed && t == "cb");
  assert(k.pendingKey(now + 10s) == -1);
  // Cycling wraps; a different key always starts a new letter.
  t.clear();
  for (int i = 0; i < 5; ++i) k.press(T9Keypad::Pqrs, t, 24, now + 20s + i * 100ms);
  assert(t == "p");  // p q r s p
  k.press(T9Keypad::Tuv, t, 24, now + 20600ms);
  assert(t == "pt");
  // Space, case, digits.
  k.press(T9Keypad::Space, t, 24, now + 30s);
  assert(t == "pt ");
  assert(k.label(T9Keypad::Case) == "ABC");
  k.press(T9Keypad::Case, t, 24, now + 31s);
  assert(k.upper() && k.label(T9Keypad::Ghi) == "GHI" && k.label(T9Keypad::Case) == "abc");
  k.press(T9Keypad::Ghi, t, 24, now + 32s);
  assert(t == "pt G");
  k.press(T9Keypad::Digits, t, 24, now + 33s);
  assert(k.digits() && k.label(T9Keypad::Wxyz) == "9" && k.label(T9Keypad::Stops) == "0");
  k.press(T9Keypad::Wxyz, t, 24, now + 34s);
  k.press(T9Keypad::Wxyz, t, 24, now + 34100ms);  // digits never cycle
  assert(t == "pt G99");
  // Delete, clear, done, limit.
  assert(k.press(T9Keypad::Delete, t, 24, now + 40s) == T9Result::Typed && t == "pt G9");
  assert(k.press(T9Keypad::Done, t, 24, now + 41s) == T9Result::Done);
  assert(k.press(T9Keypad::Clear, t, 24, now + 42s) == T9Result::Typed && t.empty());
  T9Keypad small;
  std::string s;
  small.press(T9Keypad::Abc, s, 2, now);
  small.press(T9Keypad::Def, s, 2, now + 100ms);
  assert(small.press(T9Keypad::Ghi, s, 2, now + 200ms) == T9Result::None && s == "ad");
  assert(small.press(T9Keypad::Def, s, 2, now + 300ms) == T9Result::Typed && s == "ae");  // cycling still works at the limit
  assert(small.press(99, s, 2, now) == T9Result::None);
  std::puts("Books T9 keypad: PASS");
  return 0;
}
