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

  // EPUB image rasterization must clip at the 375px logical canvas edge
  // without corrupting padding or a neighboring row.
  const uint8_t solidInk[] = {0x00};
  display.drawImage(solidInk, 372, 0, 8, 1);
  assert(display.getFrameBuffer()[46] == 0xf1);
  assert(display.getFrameBuffer()[47] == 0xff);
  display.clearScreen();
  display.drawImage(solidInk, 374, 319, 8, 2);
  assert(display.getFrameBuffer()[(319 * 47) + 46] == 0xfd);

  // Non-byte-aligned placement preserves the adjacent background pixels.
  display.clearScreen();
  const uint8_t checkered[] = {0xaa};
  display.drawImage(checkered, 5, 2, 8, 1);
  assert(display.getFrameBuffer()[2 * 47] == 0xfd);
  assert(display.getFrameBuffer()[2 * 47 + 1] == 0x57);

  return 0;
}
