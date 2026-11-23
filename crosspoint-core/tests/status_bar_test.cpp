// SPDX-License-Identifier: MIT
// Reading footer status: clock form, battery percent and charging from a
// fake sysfs tree (BOOKS_SYS_ROOT), and change detection.
#include "StatusBar.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {
std::string root;
void writeFile(const std::string& rel, const char* text) {
  std::string path = root;
  for (size_t i = 1; i < rel.size(); ++i)
    if (rel[i] == '/') ::mkdir((root + rel.substr(0, i)).c_str(), 0755);
  std::FILE* f = std::fopen((root + rel).c_str(), "w");
  assert(f);
  std::fputs(text, f);
  std::fclose(f);
}
}  // namespace

int main() {
  char tmpl[] = "/tmp/books-status-XXXXXX";
  root = ::mkdtemp(tmpl);
  ::setenv("BOOKS_SYS_ROOT", root.c_str(), 1);
  ::setenv("TZ", "UTC", 1);
  ::tzset();

  // No battery files (the host): unknown, and no battery label.
  books::DeviceStatus s = books::readDeviceStatus(6 * 3600 + 5 * 60);
  assert(s.battery == -1 && !s.charging && books::batteryLabel(s).empty());
  assert(books::clockLabel(s) == "06:05");

  // The fuel gauge alone, then the power-supply class taking precedence.
  writeFile("/sys/devices/i2c-0/0-0062/cw2015_capacity", "41\n");
  assert(books::readBatteryPercent() == 41);
  writeFile("/sys/class/power_supply/battery/capacity", "87\n");
  writeFile("/sys/class/power_supply/battery/status", "Discharging\n");
  s = books::readDeviceStatus(18 * 3600 + 14 * 60 + 59);
  assert(s.battery == 87 && !s.charging);
  assert(books::clockLabel(s) == "18:14" && books::batteryLabel(s) == "87%");

  writeFile("/sys/class/power_supply/battery/status", "Charging\n");
  assert(books::readDeviceStatus(0).charging);
  writeFile("/sys/class/power_supply/battery/status", "Full\n");
  assert(books::readDeviceStatus(0).charging);

  // Out-of-range or junk values are clamped or skipped.
  writeFile("/sys/class/power_supply/battery/capacity", "140\n");
  assert(books::readBatteryPercent() == 100);
  writeFile("/sys/class/power_supply/battery/capacity", "junk\n");
  assert(books::readBatteryPercent() == 41);  // falls back to the gauge

  // A new minute is a change; seconds within the minute are not.
  const auto a = books::readDeviceStatus(18 * 3600 + 14 * 60 + 1);
  const auto b = books::readDeviceStatus(18 * 3600 + 14 * 60 + 58);
  const auto c = books::readDeviceStatus(18 * 3600 + 15 * 60);
  assert(a == b && a != c);

  // Unreadable clock, as Q2 Pod's bar shows it.
  books::DeviceStatus none;
  assert(books::clockLabel(none) == "--:--");

  // Battery fill: empty at 0, at least a pixel above 0, full at 100.
  assert(books::batteryFill(0, 10) == 0 && books::batteryFill(1, 10) == 1);
  assert(books::batteryFill(50, 10) == 5 && books::batteryFill(100, 10) == 10);

  std::puts("Books reading footer status: OK");
  return 0;
}
