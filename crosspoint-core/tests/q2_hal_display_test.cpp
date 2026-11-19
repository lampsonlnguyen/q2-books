#include <HalDisplay.h>
#include <cassert>
#include <cstdint>
int main() {
  HalDisplay display;
  display.begin();
  static_assert(HalDisplay::DISPLAY_WIDTH == 375);
  static_assert(HalDisplay::DISPLAY_HEIGHT == 320);
  static_assert(HalDisplay::DISPLAY_WIDTH_BYTES == 47);
  assert(display.getFrameBuffer() != nullptr);
  assert(display.getBufferSize() == 47 * 320);
  display.clearScreen();
  assert(display.getFrameBuffer()[0] == 0xff);
  const uint8_t bitmap[] = {0x7f}; // black ink top-left, white elsewhere
  display.drawImage(bitmap, 0, 0, 8, 1);
  assert(display.getFrameBuffer()[0] == 0x7f);
  uint32_t bytes=0;
  uint8_t* loan=display.lendFrameBufferStorage(&bytes);
  assert(loan != nullptr && bytes == display.getBufferSize());
  assert(display.lendFrameBufferStorage(nullptr) == nullptr);
  display.returnFrameBufferStorage();
  assert(display.getFrameBuffer()[0] == 0xff);
  return 0;
}
