// SPDX-License-Identifier: MIT
#pragma once
#include "Q2Controls.hpp"
#include <atomic>
#include <deque>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
namespace books {
// Q2 Pod's existing game socket protocol, re-used only for event delivery.
// Examples: "w -45", "k d 218", "k u 170", "t d 50 200".
bool parse_q2_input(std::string_view wire, PhysicalEvent& result);

// Independent Linux socket receiver for a standalone Books process.
// Does not depend on Q2 Pod's card-app runtime or its graphics libraries.
// Q2 Pod sends with MSG_DONTWAIT and a datagram socket queues only
// net.unix.max_dgram_qlen (10) of them, so input arriving while the reader is
// busy (a chapter opening, a download) was dropped by the kernel. A thread
// drains the socket as datagrams arrive into a bounded queue that merges
// consecutive wheel travel and touch moves; receive() takes from that queue.
class Q2SocketInput {
 public:
  Q2SocketInput() = default;
  ~Q2SocketInput();
  Q2SocketInput(const Q2SocketInput&) = delete;
  Q2SocketInput& operator=(const Q2SocketInput&) = delete;

  bool open(std::string path = "/tmp/q2-books.input.sock");
  bool receive(PhysicalEvent& result);
  // 1 when a datagram is ready, 0 on timeout or signal, -1 on a socket error.
  int waitForInput(int timeoutMs) const;
  void close();
  static constexpr size_t maxQueued = 512;
 private:
  void drain();
  bool push(const PhysicalEvent& event);
  int fd_ = -1;
  int wake_[2] = {-1, -1};  // drain thread -> waitForInput
  int stop_[2] = {-1, -1};  // close() -> drain thread
  std::string path_;
  mutable std::mutex lock_;
  std::deque<PhysicalEvent> queue_;
  std::atomic<bool> failed_{false};
  std::thread thread_;
};
}  // namespace books
