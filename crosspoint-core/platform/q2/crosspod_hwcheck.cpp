// SPDX-License-Identifier: MIT
// CrossPod hardware diagnostic ONLY. Does not open or render EPUB files.
// Run via q2remote/SSH while Q2 Pod isn't drawing; wait for clean exit.
#include "Q2Framebuffer.hpp"
#include "Q2SocketInput.hpp"
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <glob.h>
#include <sys/resource.h>
#include <string_view>
#include <thread>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop_handler(int) { stopped = 1; }
constexpr unsigned W = crosspod::Q2Framebuffer::LogicalWidth;
constexpr unsigned H = crosspod::Q2Framebuffer::LogicalHeight;
constexpr unsigned MaxSeconds = 30;
struct Metrics {
  struct rusage usage{};
  std::chrono::steady_clock::time_point time{};
  int battery=-1;
};
int batteryPercent() {
  glob_t matches{};
  int result=-1;
  if (::glob("/sys/class/power_supply/*/capacity", 0, nullptr, &matches)==0) {
    for(size_t i=0;i<matches.gl_pathc;++i) {
      std::FILE* file=std::fopen(matches.gl_pathv[i],"r");
      if (!file) continue;
      int percent=-1;
      if (std::fscanf(file,"%d",&percent)==1 && percent>=0 && percent<=100) {
        result=percent;
        std::fclose(file);
        break;
      }
      std::fclose(file);
    }
  }
  ::globfree(&matches);
  return result;
}
Metrics snapshot() {
  Metrics metric{};
  metric.time=std::chrono::steady_clock::now();
  ::getrusage(RUSAGE_SELF,&metric.usage);
  metric.battery=batteryPercent();
  return metric;
}
double cpuSeconds(const timeval& value) {
  return static_cast<double>(value.tv_sec) +
         static_cast<double>(value.tv_usec)/1000000.0;
}
}

int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--display" && std::string_view(argv[1]) != "--card")) {
    std::fprintf(stderr, "Usage: crosspod-hwcheck [--display|--card]\n"
            "Default: check input socket only. --display briefly uses /dev/fb0.\n");
    return 2;
  }
  const bool card = argc == 2 && std::string_view(argv[1]) == "--card";
  const bool show = argc == 2;
  // Avoid drawing over Q2 Pod by default. Only use --display with Q2 Pod
  // screen painting deliberately suspended by the hardware test operator.
  crosspod::Q2Framebuffer framebuffer;
  std::array<uint32_t, W*H> image{};
  if (show) {
    if (!framebuffer.open()) {
      std::fprintf(stderr, "Q2 /dev/fb0 did not match expected 320x375 32-bit double buffer\n");
      return 3;
    }
    for (unsigned y=0; y<H; ++y) for (unsigned x=0; x<W; ++x) {
      const unsigned band = (x*6)/W;
      static constexpr uint32_t bands[] = {
          0xff161616u, 0xffebebebu, 0xffff4444u,
          0xff44ff44u, 0xff4488ffu, 0xffffcc44u};
      image[y*W+x] = bands[band];
    }
    if (!framebuffer.presentARGB(image.data(), image.size())) {
      std::fprintf(stderr, "Framebuffer pan failed\n");
      return 4;
    }
  }
  crosspod::Q2SocketInput input;
  if (!input.open(card ? "/tmp/q2-crosspod-test.input.sock" : "/tmp/q2-crosspod.input.sock")) {
    std::fprintf(stderr, "Input socket creation failed\n");
    return 5;
  }
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);
  std::fprintf(stderr, "CrossPod Q2 hardware diagnostic: socket ready; "
           "timeout %u s; exit on Return key up (170).\n", MaxSeconds);
  const Metrics startMetric = snapshot();
  const auto start = startMetric.time;
  if (card) std::fprintf(stderr, "CrossPod card diagnostic: press Return to exit, or wait 30 seconds.\\n");
  unsigned events = 0;
  unsigned wheelEvents=0, keyEvents=0, touchEvents=0;
  while (!stopped && std::chrono::steady_clock::now()-start < std::chrono::seconds(MaxSeconds)) {
    crosspod::PhysicalEvent event;
    if (input.receive(event)) {
      ++events;
      if (event.kind == crosspod::PhysicalKind::WheelStep) ++wheelEvents;
      else if (event.kind == crosspod::PhysicalKind::KeyDown ||
               event.kind == crosspod::PhysicalKind::KeyUp) ++keyEvents;
      else ++touchEvents;
      std::fprintf(stderr, "event %u kind=%u code=%d x=%d y=%d\n",
                   events, static_cast<unsigned>(event.kind),
                   event.value, event.x, event.y);
      if (event.kind == crosspod::PhysicalKind::KeyUp && event.value == 170) break;
    } else {
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  }
  input.close();
  framebuffer.close();
  const Metrics endMetric = snapshot();
  const double wall = std::chrono::duration<double>(endMetric.time - startMetric.time).count();
  const double cpu = cpuSeconds(endMetric.usage.ru_utime) +
                     cpuSeconds(endMetric.usage.ru_stime) -
                     cpuSeconds(startMetric.usage.ru_utime) -
                     cpuSeconds(startMetric.usage.ru_stime);
  const double cpuPct = wall > 0 ? 100.0 * cpu / wall : 0.0;
  std::fprintf(stderr, "CrossPod diagnostic closed normally; events=%u\n", events);
  // Machine-readable telemetry for later physical Q2 validation. Power
  // capacity is best effort: -1 denotes unavailable, not a failed battery.
  std::fprintf(stderr,
      "CROSSPOD_METRICS {\"wall_s\":%.3f,\"cpu_s\":%.3f,"
      "\"cpu_pct\":%.2f,\"peak_rss_kib\":%ld,"
      "\"input_events\":%u,\"wheel_events\":%u,"
      "\"key_events\":%u,\"touch_events\":%u,"
      "\"battery_start_pct\":%d,\"battery_end_pct\":%d}\n",
      wall,cpu,cpuPct,endMetric.usage.ru_maxrss,events,wheelEvents,
      keyEvents,touchEvents,startMetric.battery,endMetric.battery);
  return 0;
}
