// SPDX-License-Identifier: MIT
// Reading footer status: the clock and battery Q2 Pod's own bar shows.
// Clock: local time through localtime_r, as Q2 Pod's bar reads it; the card
// app inherits Q2 Pod's TZ. 24-hour form ("18:14"): Q2 Pod's 12/24 setting
// lives in firmware memory only, so a card app cannot follow it.
// Battery: the CW2015 fuel gauge's percent and the charger's state, from
// sysfs. BOOKS_SYS_ROOT prefixes those paths (tests).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

namespace books {

struct DeviceStatus {
  int hour = -1, minute = -1;  // -1: clock unreadable
  int battery = -1;            // 0..100, -1: unknown
  bool charging = false;
  bool operator==(const DeviceStatus& o) const {
    return hour == o.hour && minute == o.minute && battery == o.battery && charging == o.charging;
  }
  bool operator!=(const DeviceStatus& o) const { return !(*this == o); }
};

inline std::string sysPath(const char* path) {
  const char* root = std::getenv("BOOKS_SYS_ROOT");
  return std::string(root ? root : "") + path;
}

inline bool readSmallFile(const std::string& path, char* buf, size_t size) {
  std::FILE* f = std::fopen(path.c_str(), "r");
  if (!f) return false;
  const size_t n = std::fread(buf, 1, size - 1, f);
  std::fclose(f);
  buf[n] = '\0';
  return n > 0;
}

// Percent 0..100, or -1. Prefers the power-supply class, then the fuel gauge.
inline int readBatteryPercent() {
  static const char* const kPaths[] = {"/sys/class/power_supply/battery/capacity",
                                       "/sys/devices/i2c-0/0-0062/cw2015_capacity"};
  for (const char* p : kPaths) {
    char buf[32];
    if (!readSmallFile(sysPath(p), buf, sizeof buf)) continue;
    char* end = nullptr;
    const long v = std::strtol(buf, &end, 10);
    if (end == buf) continue;
    return v < 0 ? 0 : v > 100 ? 100 : static_cast<int>(v);
  }
  return -1;
}

inline bool readCharging() {
  char buf[32];
  if (!readSmallFile(sysPath("/sys/class/power_supply/battery/status"), buf, sizeof buf)) return false;
  return std::strncmp(buf, "Charging", 8) == 0 || std::strncmp(buf, "Full", 4) == 0;
}

inline DeviceStatus readDeviceStatus(std::time_t now = std::time(nullptr)) {
  DeviceStatus s;
  std::tm tm{};
  if (now != static_cast<std::time_t>(-1) && localtime_r(&now, &tm) && tm.tm_hour >= 0 && tm.tm_hour < 24 &&
      tm.tm_min >= 0 && tm.tm_min < 60) {
    s.hour = tm.tm_hour;
    s.minute = tm.tm_min;
  }
  s.battery = readBatteryPercent();
  s.charging = s.battery >= 0 && readCharging();
  return s;
}

// "18:14", or "--:--" as Q2 Pod's bar shows an unreadable clock.
inline std::string clockLabel(const DeviceStatus& s) {
  if (s.hour < 0) return "--:--";
  char buf[8];
  std::snprintf(buf, sizeof buf, "%02d:%02d", s.hour, s.minute);
  return buf;
}

inline std::string batteryLabel(const DeviceStatus& s) {
  return s.battery < 0 ? std::string() : std::to_string(s.battery) + "%";
}

// Pixels of the battery bar's fill, for an inner width w (at least one
// pixel shows above 0 %, so a nearly flat battery is still drawn).
inline int batteryFill(int percent, int w) {
  if (percent <= 0 || w <= 0) return 0;
  const int fill = (percent * w + 50) / 100;
  return fill < 1 ? 1 : fill > w ? w : fill;
}

}  // namespace books
