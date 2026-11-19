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
#include <thread>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stop_handler(int) { stopped = 1; }
constexpr unsigned W = crosspod::Q2Framebuffer::LogicalWidth;
constexpr unsigned H = crosspod::Q2Framebuffer::LogicalHeight;
constexpr unsigned MaxSeconds = 30;
}

int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--display")) {
    std::fprintf(stderr, "Usage: crosspod-hwcheck [--display]\n"
            "Default: check input socket only. --display briefly uses /dev/fb0.\n");
    return 2;
  }
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
  if (!input.open()) {
    std::fprintf(stderr, "Input socket creation failed\n");
    return 5;
  }
  std::signal(SIGINT, stop_handler);
  std::signal(SIGTERM, stop_handler);
  std::fprintf(stderr, "CrossPod Q2 hardware diagnostic: socket ready; "
           "timeout %u s; exit on Return key up (170).\n", MaxSeconds);
  const auto start = std::chrono::steady_clock::now();
  unsigned events = 0;
  while (!stopped && std::chrono::steady_clock::now()-start < std::chrono::seconds(MaxSeconds)) {
    crosspod::PhysicalEvent event;
    if (input.receive(event)) {
      ++events;
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
  std::fprintf(stderr, "CrossPod diagnostic closed normally; events=%u\n", events);
  return 0;
}
