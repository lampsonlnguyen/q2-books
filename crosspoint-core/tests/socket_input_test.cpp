#include <Q2SocketInput.hpp>
#include <cassert>
#include <cstring>
#include <cstdio>
#include <string>
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
