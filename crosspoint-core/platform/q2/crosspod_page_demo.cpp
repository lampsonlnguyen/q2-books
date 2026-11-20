// SPDX-License-Identifier: MIT
// Offline Q2 hardware page preview. This NEVER parses EPUB nor flashes firmware.
// Supply the exact PGM output produced by original CrossPoint host renderer.
#include "Q2Framebuffer.hpp"
#include <array>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {
volatile std::sig_atomic_t interrupted = 0;
void onSignal(int) { interrupted = 1; }
constexpr size_t W = crosspod::Q2Framebuffer::LogicalWidth;
constexpr size_t H = crosspod::Q2Framebuffer::LogicalHeight;
constexpr size_t Size = W * H;
}

int main(int argc, char** argv) {
  if (argc != 3 ||
      (std::strcmp(argv[1], "--check") && std::strcmp(argv[1], "--display"))) {
    std::fprintf(stderr,
      "Usage: crosspod-page-demo --check|--display <crosspod-vietnamese-preview.pgm>\n"
      "  --check: validate image and architecture without framebuffer access\n"
      "  --display: own /dev/fb0 for <=20s; Q2 Pod must stop painting first\n");
    return 2;
  }
  std::ifstream file(argv[2], std::ios::binary);
  if (!file) { std::perror("Unable to open PGM"); return 3; }
  // Producer emits an exact fixed-size P5 header. Reject arbitrary files,
  // oversized allocations and malformed PGM rather than trying to decode.
  std::string header;
  for (int i=0; i<3; ++i) {
    std::string line;
    if (!std::getline(file,line) || line.size()>80) return 4;
    header += line + "\n";
  }
  if (header != "P5\n375 320\n255\n") {
    std::fputs("Invalid CrossPod 375x320 grayscale PGM\n",stderr);
    return 4;
  }
  std::vector<unsigned char> gray(Size);
  if (!file.read(reinterpret_cast<char*>(gray.data()),static_cast<std::streamsize>(gray.size()))) {
    std::fputs("Truncated PGM\n",stderr); return 4;
  }
  if (file.peek() != std::char_traits<char>::eof()) {
    std::fputs("PGM contains trailing bytes\n",stderr); return 4;
  }
  size_t ink=0;
  for (auto px:gray) if(px<128) ++ink;
  if (ink < 200) { std::fputs("Blank/invalid page\n",stderr); return 4; }
  std::printf("CrossPod page verified: %zux%zu, %zu dark pixels\n",W,H,ink);
  if (!std::strcmp(argv[1],"--check")) return 0;

  // Allocate only after --display; reject unknown framebuffer geometry.
  std::vector<uint32_t> argb(Size);
  for (size_t i=0;i<Size;++i) {
    const uint32_t g=gray[i];
    argb[i]=0xff000000u|(g<<16)|(g<<8)|g;
  }
  crosspod::Q2Framebuffer fb;
  if (!fb.open()) {
    std::fputs("Could not open matching Q2 /dev/fb0. No display changes made.\n",stderr);
    return 5;
  }
  std::signal(SIGINT,onSignal);
  std::signal(SIGTERM,onSignal);
  if (!fb.presentARGB(argb.data(),argb.size())) {
    std::fputs("Framebuffer update failed\n",stderr);
    return 6;
  }
  std::puts("Vietnamese CrossPoint page on Q2 LCD for at most 20 seconds; Ctrl-C exits.");
  std::fflush(stdout);
  const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(20);
  while (!interrupted && std::chrono::steady_clock::now()<until)
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  fb.close();
  std::puts("CrossPod page demo exited; original framebuffer page restored.");
  return 0;
}
