#include <Q2SocketInput.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <csignal>
#include <ctime>
#include <pthread.h>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
using namespace crosspod;
int main() {
  PhysicalEvent event{};
  assert(parse_q2_input("w -60", event));
  assert(event.kind == PhysicalKind::WheelStep && event.value == -60);
  assert(parse_q2_input("k u 170", event));
  assert(event.kind == PhysicalKind::KeyUp && event.value == 170);
  assert(parse_q2_input("k d 218", event));
  assert(event.kind == PhysicalKind::KeyDown && event.value == 218);
  assert(parse_q2_input("t m 374 319", event));
  assert(event.kind == PhysicalKind::TouchMove && event.x == 374 && event.y == 319);
  for (auto invalid : {"w abc", "k z 170", "k u -1", "t d 375 20",
                        "t u -1 20", "w 5 extra", "k u 170 extra", "q"}) {
    assert(!parse_q2_input(invalid, event));
  }
  const std::string path = "/tmp/crosspod-input-test-" + std::to_string(getpid()) + ".sock";
  Q2SocketInput receiver;
  assert(receiver.open(path));
  // A second reader must never unlink the first reader's live endpoint.
  Q2SocketInput competing;
  assert(!competing.open(path));
  assert(::access(path.c_str(), F_OK) == 0);
  assert(!receiver.receive(event)); // nonblocking and no data
  assert(receiver.waitForInput(10) == 0);
  int sender = ::socket(AF_UNIX, SOCK_DGRAM, 0);
  assert(sender >= 0);
  sockaddr_un addr{};
  addr.sun_family = AF_UNIX;
  std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
  const char msg[] = "k u 172";
  assert(::sendto(sender, msg, sizeof(msg) - 1, 0,
                  reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == sizeof(msg) - 1);
  assert(receiver.waitForInput(1000) == 1);
  assert(receiver.receive(event));
  assert(receiver.waitForInput(0) == 0);
  assert(event.kind == PhysicalKind::KeyUp && event.value == 172);
  // Q2 Pod sends with MSG_DONTWAIT; the kernel queues only max_dgram_qlen (10)
  // datagrams. A burst while the reader is busy must not be lost.
  for (int i = 0; i < 100; ++i) {
    const char key[] = "k u 173";
    for (int tries = 0; ::sendto(sender, key, sizeof(key) - 1, MSG_DONTWAIT,
                                 reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0; ++tries) {
      assert(tries < 2000);  // the drain thread empties the queue within microseconds
      usleep(100);
    }
  }
  for (int i = 0; i < 50; ++i) {
    const char wheel[] = "w -4";
    while (::sendto(sender, wheel, sizeof(wheel) - 1, MSG_DONTWAIT,
                    reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) usleep(100);
  }
  usleep(200000);  // the reader was "busy": nothing received yet
  int keys = 0, travel = 0, wheelEvents = 0;
  while (receiver.receive(event)) {
    if (event.kind == PhysicalKind::KeyUp) ++keys;
    if (event.kind == PhysicalKind::WheelStep) { travel += event.value; ++wheelEvents; }
  }
  assert(keys == 100);
  assert(travel == -200 && wheelEvents >= 1 && wheelEvents < 50);  // merged, none lost
  assert(receiver.waitForInput(0) == 0);
  // SIGTERM must still interrupt the main thread's wait (the drain thread blocks it).
  struct sigaction quiet{};
  quiet.sa_handler = [](int) {};
  sigaction(SIGUSR1, &quiet, nullptr);
  sigaction(SIGTERM, &quiet, nullptr);
  pthread_t self = pthread_self();
  std::thread([self] { usleep(100000); pthread_kill(self, SIGTERM); }).detach();
  timespec start{}, end{};
  clock_gettime(CLOCK_MONOTONIC, &start);
  assert(receiver.waitForInput(3000) == 0);
  clock_gettime(CLOCK_MONOTONIC, &end);
  assert(end.tv_sec - start.tv_sec < 2);
  ::close(sender);
  receiver.close();
  assert(receiver.waitForInput(0) == -1);
  assert(::access(path.c_str(), F_OK) != 0);
  // A crashed reader can leave a stale socket pathname behind.
  int stale=::socket(AF_UNIX,SOCK_DGRAM,0);
  assert(stale>=0);
  assert(::bind(stale,reinterpret_cast<sockaddr*>(&addr),sizeof(addr))==0);
  ::close(stale);
  assert(receiver.open(path));
  receiver.close();
  // Never unlink a regular file that happens to have the socket name.
  FILE* regular=std::fopen(path.c_str(),"wb");
  assert(regular);
  assert(std::fclose(regular)==0);
  assert(!receiver.open(path));
  assert(::access(path.c_str(),F_OK)==0);
  assert(::unlink(path.c_str())==0);
  return 0;
}
