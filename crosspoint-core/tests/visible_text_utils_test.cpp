// SPDX-License-Identifier: MIT
#include <Epub/VisibleTextUtils.h>
#include <cassert>
int main() {
  using VisibleTextUtils::isNonVisibleElement;
  assert(isNonVisibleElement("head"));
  assert(isNonVisibleElement("HEAD"));
  assert(isNonVisibleElement("title"));
  assert(isNonVisibleElement("script"));
  assert(isNonVisibleElement("style"));
  assert(isNonVisibleElement("rp"));
  assert(!isNonVisibleElement("ruby"));
  assert(!isNonVisibleElement("rt"));
  assert(!isNonVisibleElement("body"));
  assert(!isNonVisibleElement("p"));
  assert(!isNonVisibleElement("h1"));
}
