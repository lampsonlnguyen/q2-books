#include <OtTable.h>
#include <cassert>
#include <cstdint>
#include <cstdio>

// Exercise the original OpenType table reader with malformed and valid inputs.
// This test is host-only; it does not imply that the Q2 renderer is linked.
int main() {
  const uint8_t bytes[] = {0x00, 0x03, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06};
  const ot::Table table(bytes, sizeof(bytes));
  assert(table.u16(0) == 3);
  assert(table.u16(7) == 0);
  assert(table.u32(6) == 0);
  assert(table.count16(0, 2, 2) == 3);
  assert(table.count16(0, 2, 3) == 2);
  assert(table.count16(0, 2, 0) == 0);
  assert(table.count16(0, 20, 2) == 0);
  assert(table.at(0).empty());
  assert(table.at(8).empty());
  assert(table.at(2).length() == 6);
  assert(ot::Table(nullptr, 20).empty());
  std::puts("CrossPod OpenType table bounds checks OK");
  return 0;
}
