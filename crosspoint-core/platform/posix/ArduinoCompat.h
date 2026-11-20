#pragma once
// POSIX timing compatibility for original CrossPoint chapter parser.
// Avoids ESP32 timer imports on Q2 Linux.
#include <chrono>
#include <thread>
inline unsigned long millis() {
  using namespace std::chrono;
  return static_cast<unsigned long>(
      duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count());
}
inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
