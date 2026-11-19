// SPDX-License-Identifier: MIT
#include "Q2SocketInput.hpp"
#include <charconv>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace crosspod {
namespace {
bool read_int(std::string_view& text, int& out) {
  while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
  if (text.empty()) return false;
  size_t len = text.find(' ');
  if (len == std::string_view::npos) len = text.size();
  const char* first = text.data();
  const auto converted = std::from_chars(first, first + len, out);
  if (converted.ec != std::errc{} || converted.ptr != first + len) return false;
  text.remove_prefix(len);
  return true;
}
bool finished(std::string_view s) {
  while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
  return s.empty();
}
}

bool parse_q2_input(std::string_view wire, PhysicalEvent& result) {
  if (wire.size() < 3 || wire.size() > 64 || wire.find('\0') != std::string_view::npos)
    return false;
  PhysicalEvent temp{};
  if (wire.substr(0, 2) == "w ") {
    temp.kind = PhysicalKind::WheelStep;
    if (!read_int(wire = wire.substr(2), temp.value) || !finished(wire)) return false;
  } else if (wire.substr(0, 2) == "k " && wire.size() >= 5 && wire[3] == ' ') {
    if (wire[2] != 'd' && wire[2] != 'u') return false;
    temp.kind = wire[2] == 'd' ? PhysicalKind::KeyDown : PhysicalKind::KeyUp;
    if (!read_int(wire = wire.substr(4), temp.value) || !finished(wire) ||
        temp.value < 0 || temp.value > 65535) return false;
  } else if (wire.substr(0, 2) == "t " && wire.size() >= 5 && wire[3] == ' ') {
    if (wire[2] != 'd' && wire[2] != 'm' && wire[2] != 'u') return false;
    temp.kind = wire[2] == 'd' ? PhysicalKind::TouchDown :
                wire[2] == 'm' ? PhysicalKind::TouchMove : PhysicalKind::TouchUp;
    wire.remove_prefix(4);
    if (!read_int(wire, temp.x) || !read_int(wire, temp.y) || !finished(wire) ||
        temp.x < 0 || temp.y < 0 || temp.x >= 375 || temp.y >= 320) return false;
  } else {
    return false;
  }
  result = temp;
  return true;
}
Q2SocketInput::~Q2SocketInput() { close(); }
bool Q2SocketInput::open(std::string path) {
  close();
  if (path.empty() || path.size() >= sizeof(sockaddr_un::sun_path)) return false;
  int fd = ::socket(AF_UNIX, SOCK_DGRAM, 0);
  if (fd < 0) return false;
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  ::unlink(path.c_str());  // stale socket from a crashed reader
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0 ||
      ::chmod(path.c_str(), 0600) != 0) {
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  const int flags = ::fcntl(fd, F_GETFL);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  fd_ = fd;
  path_ = std::move(path);
  return true;
}
bool Q2SocketInput::receive(PhysicalEvent& result) {
  if (fd_ < 0) return false;
  char data[96];
  const ssize_t n = ::recv(fd_, data, sizeof(data), 0);
  if (n < 0) return false;  // nonblocking EAGAIN or transport error
  if (n == 0) return false;
  return parse_q2_input(std::string_view(data, static_cast<size_t>(n)), result);
}
void Q2SocketInput::close() {
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
    ::unlink(path_.c_str());
    path_.clear();
  }
}
} // namespace crosspod
