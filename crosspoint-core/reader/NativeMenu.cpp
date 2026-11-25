// SPDX-License-Identifier: MIT
#include "NativeMenu.hpp"
#include "ShelfHistory.hpp"
#include "UiLanguage.hpp"
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

namespace books {
namespace {
constexpr char kNativeSocket[]="/tmp/q2-books.browse.sock";
constexpr char kNativePid[]="/tmp/q2-books";
constexpr char kNativeLock[]="/tmp/q2-books.browse.lock";
constexpr std::string_view kBookDirectory="/mnt/mmc/Books/";
constexpr auto kNativeIdle=std::chrono::minutes(10);
using Clock=std::chrono::steady_clock;

struct Fd {
  int fd;
  ~Fd() {if(fd>=0)close(fd);}
};

bool nativeScreensAvailable() {
  std::ifstream caps("/tmp/q2-addon.caps");
  std::string word;
  while(caps>>word)if(word=="screen")return true;
  return false;
}

void appendRow(std::string& reply,std::string_view kind,std::string_view uri,const char* label) {
  reply.append(kind).append("\t").append(uri).append("\t");
  for(const char* p=tr(label);*p;++p) {
    switch(*p) {
      case '%':reply+="%25";break;
      case '\t':reply+="%09";break;
      case '\n':reply+="%0A";break;
      case '\r':reply+="%0D";break;
      default:reply+=*p;
    }
  }
  reply+="\t\t\t\n";
}

void serveRequest(int fd) {
  const timeval timeout={1,0};
  if(setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))<0 ||
     setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout))<0)return;
  char request[8192];
  size_t used=0;
  const auto deadline=Clock::now()+std::chrono::seconds(2);
  while(used<sizeof(request) && Clock::now()<deadline) {
    const ssize_t n=recv(fd,request+used,sizeof(request)-used,0);
    if(n<=0)break;
    used+=static_cast<size_t>(n);
    if(std::string_view(request,used).find('\n',9)!=std::string_view::npos)break;
  }
  if(!used)return;  // The firmware checks whether the socket is listening.
  const bool canResume=[] {
    for(const auto& path:loadRecentBooks()) {
      struct stat info{};
      if(path.compare(0,kBookDirectory.size(),kBookDirectory)==0 &&
         stat(path.c_str(),&info)==0 && S_ISREG(info.st_mode))return true;
    }
    return false;
  }();
  const auto reply=nativeMenuReply(std::string_view(request,used),nativeScreensAvailable(),canResume);
  for(size_t offset=0;offset<reply.size();) {
    const ssize_t sent=send(fd,reply.data()+offset,reply.size()-offset,MSG_NOSIGNAL);
    if(sent<=0)break;
    offset+=static_cast<size_t>(sent);
  }
}
}

std::string nativeMenuReply(std::string_view request,bool canOpen,bool canResume) {
  constexpr std::string_view version="Q2SPOT/1\n";
  constexpr std::string_view bad="Q2SPOT/1\n0\tbad\t\t\n";
  if(request.substr(0,version.size())!=version)return std::string(bad);
  request.remove_prefix(version.size());
  const auto tab=request.find('\t');
  if(tab==0 || tab>20 || request.substr(0,tab).find_first_not_of("0123456789")!=std::string_view::npos)
    return std::string(bad);
  std::string reply(version);
  reply.append(request.substr(0,tab));
  if(request.substr(tab+1)!="menu\n")return reply+"\tbad\t\t\n";
  reply+="\tok\t\t\n";
  if(!canOpen) {
    appendRow(reply,"info","","Update Q2 Pod to open Books");
    return reply;
  }
  if(canResume)appendRow(reply,"screen",kNativeResume,"Continue reading");
  appendRow(reply,"screen",kNativeBrowse,"Browse files");
  appendRow(reply,"screen",kNativeHome,"Books Home");
  appendRow(reply,"screen",kNativeOptions,"Options");
  return reply;
}

int serveNativeMenu(const volatile std::sig_atomic_t& interrupted) {
  const Fd lock{open(kNativeLock,O_CREAT|O_CLOEXEC|O_NOFOLLOW,0600)};
  if(lock.fd<0)return 1;
  if(flock(lock.fd,LOCK_EX|LOCK_NB)<0)return errno==EWOULDBLOCK?0:1;

  const Fd server{socket(AF_UNIX,SOCK_STREAM|SOCK_CLOEXEC,0)};
  if(server.fd<0)return 1;
  sockaddr_un addr{};
  addr.sun_family=AF_UNIX;
  std::snprintf(addr.sun_path,sizeof(addr.sun_path),"%s",kNativeSocket);
  unlink(kNativeSocket);
  if(bind(server.fd,reinterpret_cast<const sockaddr*>(&addr),sizeof(addr))<0)return 1;
  if(listen(server.fd,4)<0) {
    unlink(kNativeSocket);
    return 1;
  }
  const int pidFd=open(kNativePid,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC|O_NOFOLLOW,0644);
  FILE* pid=pidFd<0?nullptr:fdopen(pidFd,"w");
  if(pidFd>=0 && !pid)close(pidFd);
  bool ready=false;
  if(pid) {
    ready=std::fprintf(pid,"%ld\n",static_cast<long>(getpid()))>0;
    ready=std::fclose(pid)==0 && ready;
  }
  auto lastRequest=Clock::now();
  while(ready && !interrupted && Clock::now()-lastRequest<kNativeIdle) {
    pollfd event{server.fd,POLLIN,0};
    const int result=poll(&event,1,1000);
    if(result<0 && errno==EINTR)continue;
    if(result<0 || (result>0 && !(event.revents&POLLIN)))break;
    if(result==0 || interrupted)continue;
    const Fd client{accept(server.fd,nullptr,nullptr)};
    if(client.fd>=0) {
      serveRequest(client.fd);
      lastRequest=Clock::now();
    }
  }
  unlink(kNativeSocket);
  if(pidFd>=0)unlink(kNativePid);
  return ready?0:1;
}
}
