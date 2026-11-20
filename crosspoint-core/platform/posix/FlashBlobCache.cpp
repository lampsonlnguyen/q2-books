// SPDX-License-Identifier: MIT
// Q2 Linux hardware adapter: ESP32 SPIFFS flash partitions do not exist.
// Returning false causes original SdCardFont to use its bounded in-RAM/SD
// loading path rather than writing to or probing arbitrary Q2 flash devices.
#include "FlashBlobCache.h"
namespace FlashBlobCache {
const uint8_t* acquire(uint32_t, uint32_t, Reader, void*) { return nullptr; }
void release(void*) {}
bool canHold(uint32_t, uint32_t) { return false; }
}
