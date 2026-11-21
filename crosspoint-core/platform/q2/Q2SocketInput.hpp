// SPDX-License-Identifier: MIT
#pragma once
#include "Q2Controls.hpp"
#include <string>
#include <string_view>
namespace crosspod {
// Q2 Pod's existing game socket protocol, re-used only for event delivery.
// Examples: "w -45", "k d 218", "k u 170", "t d 50 200".
bool parse_q2_input(std::string_view wire, PhysicalEvent& result);

// Independent Linux socket receiver for a standalone CrossPod process.
// Does not depend on Q2 Pod's card-app runtime or its graphics libraries.
class Q2SocketInput {
 public:
  Q2SocketInput() = default;
  ~Q2SocketInput();
  Q2SocketInput(const Q2SocketInput&) = delete;
  Q2SocketInput& operator=(const Q2SocketInput&) = delete;

  bool open(std::string path = "/tmp/q2-crosspod.input.sock");
  bool receive(PhysicalEvent& result);
  // 1 when a datagram is ready, 0 on timeout or signal, -1 on a socket error.
  int waitForInput(int timeoutMs) const;
  void close();
 private:
  int fd_ = -1;
  std::string path_;
};
}  // namespace crosspod
