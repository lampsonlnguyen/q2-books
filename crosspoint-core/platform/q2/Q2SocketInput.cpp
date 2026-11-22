// SPDX-License-Identifier: MIT
#include "Q2SocketInput.hpp"
#include <charconv>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <climits>
#include <csignal>
#include <pthread.h>
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
  // Do not steal the endpoint from a running reader or delete an unrelated file.
  // A pathname socket left by a crashed process may be removed only when the
  // kernel reports that no datagram receiver is bound to it.
  struct stat previous{};
  if (::lstat(path.c_str(), &previous) == 0) {
    if (!S_ISSOCK(previous.st_mode)) { ::close(fd); return false; }
    int probe = ::socket(AF_UNIX, SOCK_DGRAM, 0);
    if (probe < 0) { ::close(fd); return false; }
    const int connected = ::connect(probe, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
    const int savedError = errno;
    ::close(probe);
    if (connected == 0 || savedError != ECONNREFUSED || ::unlink(path.c_str()) != 0) {
      ::close(fd);
      return false;
    }
  } else if (errno != ENOENT) {
    ::close(fd);
    return false;
  }
  // If another process binds in the meantime, its endpoint must remain.
  if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    return false;
  }
  if (::chmod(path.c_str(), 0600) != 0) {
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  const int flags = ::fcntl(fd, F_GETFL);
  if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
      ::pipe2(wake_, O_NONBLOCK | O_CLOEXEC) != 0) {
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  if (::pipe2(stop_, O_NONBLOCK | O_CLOEXEC) != 0) {
    ::close(wake_[0]); ::close(wake_[1]); wake_[0] = wake_[1] = -1;
    ::close(fd);
    ::unlink(path.c_str());
    return false;
  }
  fd_ = fd;
  path_ = std::move(path);
  failed_ = false;
  // SIGINT/SIGTERM must reach the main thread (its poll returns EINTR and it
  // saves the position), so the drain thread starts with them blocked.
  sigset_t blocked, oldMask;
  sigemptyset(&blocked);
  sigaddset(&blocked, SIGINT);
  sigaddset(&blocked, SIGTERM);
  pthread_sigmask(SIG_BLOCK, &blocked, &oldMask);
  try {
    thread_ = std::thread(&Q2SocketInput::drain, this);
  } catch (...) {
    pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
    close();
    return false;
  }
  pthread_sigmask(SIG_SETMASK, &oldMask, nullptr);
  return true;
}
bool Q2SocketInput::push(const PhysicalEvent& event) {
  std::lock_guard<std::mutex> guard(lock_);
  if (!queue_.empty()) {
    PhysicalEvent& last = queue_.back();
    // Merge what the reader would only add up or overwrite: wheel travel (one
    // datagram can carry several ticks) and touch moves (the latest wins).
    if (event.kind == PhysicalKind::WheelStep && last.kind == PhysicalKind::WheelStep &&
        (event.value < 0) == (last.value < 0)) {
      const long long sum = static_cast<long long>(last.value) + event.value;
      last.value = static_cast<int>(sum > INT_MAX ? INT_MAX : sum < INT_MIN ? INT_MIN : sum);
      return false;
    }
    if (event.kind == PhysicalKind::TouchMove && last.kind == PhysicalKind::TouchMove) {
      last = event;
      return false;
    }
  }
  if (queue_.size() >= maxQueued) return false;  // bounded: a flood cannot grow memory
  queue_.push_back(event);
  return true;
}
void Q2SocketInput::drain() {
  pollfd watch[2] = {{fd_, POLLIN, 0}, {stop_[0], POLLIN, 0}};
  for (;;) {
    const int ready = ::poll(watch, 2, -1);
    if (ready < 0) {
      if (errno == EINTR) continue;
      failed_ = true;
      break;
    }
    if (watch[1].revents) break;  // close()
    if (watch[0].revents & (POLLERR | POLLNVAL)) { failed_ = true; break; }
    bool any = false;
    for (;;) {
      char data[96];
      const ssize_t n = ::recv(fd_, data, sizeof(data), 0);
      if (n < 0) {
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) failed_ = true;
        break;
      }
      PhysicalEvent event{};
      if (n > 0 && parse_q2_input(std::string_view(data, static_cast<size_t>(n)), event)) {
        push(event);
        any = true;
      }
    }
    if (any) {
      const char byte = 1;
      (void)!::write(wake_[1], &byte, 1);  // a full pipe already means "ready"
    }
    if (failed_) break;
  }
  const char byte = 1;
  (void)!::write(wake_[1], &byte, 1);  // let a waiter see the failure
}
bool Q2SocketInput::receive(PhysicalEvent& result) {
  if (fd_ < 0) return false;
  std::lock_guard<std::mutex> guard(lock_);
  if (queue_.empty()) return false;
  result = queue_.front();
  queue_.pop_front();
  if (queue_.empty()) {
    char sink[64];
    while (::read(wake_[0], sink, sizeof(sink)) > 0) {
    }
  }
  return true;
}
int Q2SocketInput::waitForInput(int timeoutMs) const {
  if (fd_ < 0) return -1;
  {
    std::lock_guard<std::mutex> guard(lock_);
    if (!queue_.empty()) return 1;
  }
  if (failed_) return -1;
  pollfd descriptor{wake_[0], POLLIN, 0};
  const int result = ::poll(&descriptor, 1, timeoutMs);
  if (result < 0) return errno == EINTR ? 0 : -1;
  if (result == 0) return 0;
  if (failed_) return -1;
  std::lock_guard<std::mutex> guard(lock_);
  if (queue_.empty()) {  // a wake whose event receive() already took
    char sink[64];
    while (::read(wake_[0], sink, sizeof(sink)) > 0) {
    }
    return 0;
  }
  return 1;
}
void Q2SocketInput::close() {
  if (thread_.joinable()) {
    const char byte = 1;
    (void)!::write(stop_[1], &byte, 1);
    thread_.join();
  }
  for (int* pipe : {wake_, stop_})
    for (int i = 0; i < 2; ++i)
      if (pipe[i] >= 0) { ::close(pipe[i]); pipe[i] = -1; }
  {
    std::lock_guard<std::mutex> guard(lock_);
    queue_.clear();
  }
  if (fd_ >= 0) {
    ::close(fd_);
    fd_ = -1;
    ::unlink(path_.c_str());
    path_.clear();
  }
}
} // namespace crosspod
