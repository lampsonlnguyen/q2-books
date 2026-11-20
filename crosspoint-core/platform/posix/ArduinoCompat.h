#pragma once
// POSIX compatibility for the original CrossPoint chapter/Section parser.
// Reuse the shared Linux Arduino shim for timing: a second millis() definition
// with a different return type breaks the combined renderer build.
#include <Arduino.h>
#include <chrono>
#include <thread>
inline void delay(unsigned long ms) {
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}
