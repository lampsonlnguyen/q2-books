// SPDX-License-Identifier: MIT
#pragma once
// Q2 POSIX HTTP/1.1 transport. No ESP32 WiFi, Arduino HTTPClient or
// firmware dependencies. Intentional LAN-only HTTP: TLS is NOT implemented
// here and https:// is rejected, never silently downgraded.
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <map>
#include <netdb.h>
#include <poll.h>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace crosspod {
struct HttpUrl {
  std::string host;
  std::string port="80";
  std::string target="/";
  bool valid=false;
};
inline bool unsafeHttpString(const std::string_view s) {
  for(unsigned char ch:s)
    if(ch==0 || ch=='\r' || ch=='\n' || ch<0x20 || ch==0x7f)
      return true;
  return false;
}
inline HttpUrl parseHttpUrl(std::string_view text) {
  HttpUrl url;
  if(text.size()>1536 || unsafeHttpString(text) ||
     text.substr(0,7)!="http://")return url;
  text.remove_prefix(7);
  const auto separator=text.find_first_of("/?#");
  std::string_view authority=text.substr(0,separator);
  if(authority.empty() || authority.size()>253 ||
     authority.find('@')!=std::string_view::npos ||
     authority.find('[')!=std::string_view::npos ||
     authority.find(']')!=std::string_view::npos)return url;
  const auto colon=authority.rfind(':');
  if(colon!=std::string_view::npos) {
    const auto port=authority.substr(colon+1);
    if(port.empty() || port.size()>5)return url;
    unsigned number=0;
    for(char c:port) {
      if(c<'0'||c>'9')return url;
      number=number*10u+static_cast<unsigned>(c-'0');
    }
    if(number==0||number>65535)return url;
    url.port=std::to_string(number);
    authority=authority.substr(0,colon);
  }
  if(authority.empty())return url;
  for(unsigned char c:authority)
    if(!(std::isalnum(c)||c=='.'||c=='-'))return url;
  url.host=std::string(authority);
  std::transform(url.host.begin(),url.host.end(),url.host.begin(),
                 [](unsigned char c){return static_cast<char>(std::tolower(c));});
  if(separator!=std::string_view::npos) {
    const auto path=text.substr(separator);
    if(!path.empty()&&path.front()=='/')url.target=std::string(path);
    else if(!path.empty()&&path.front()=='?')url.target="/"+std::string(path);
  }
  const auto fragment=url.target.find('#');
  if(fragment!=std::string::npos)url.target.resize(fragment);
  if(url.target.empty())url.target="/";
  if(url.target.find('\\')!=std::string::npos)return {};
  url.valid=true;
  return url;
}
inline std::string absoluteHttpUrl(const HttpUrl& url) {
  if(!url.valid)return {};
  return "http://"+url.host+(url.port=="80"?"":":"+url.port)+url.target;
}
inline std::string normalizeHttpPath(std::string path) {
  const auto query=path.find('?');
  const std::string suffix=query==std::string::npos?"":path.substr(query);
  if(query!=std::string::npos)path.resize(query);
  std::vector<std::string> parts;
  for(size_t pos=0;pos<path.size();) {
    size_t end=path.find('/',pos);
    if(end==std::string::npos)end=path.size();
    const std::string part=path.substr(pos,end-pos);
    if(part=="..") {
      if(parts.empty())return {};
      parts.pop_back();
    } else if(!part.empty()&&part!=".") {
      // Reject encoded path traversal rather than guessing server decoding.
      std::string low=part;
      std::transform(low.begin(),low.end(),low.begin(),
        [](unsigned char c){return static_cast<char>(std::tolower(c));});
      if(low.find("%2e")!=std::string::npos ||
         low.find("%2f")!=std::string::npos ||
         low.find("%5c")!=std::string::npos)return {};
      parts.push_back(part);
    }
    if(end==path.size())break;
    pos=end+1;
  }
  std::string clean="/";
  for(size_t i=0;i<parts.size();++i) {
    if(i)clean+="/";
    clean+=parts[i];
  }
  if(!path.empty()&&path.back()=='/'&&clean.back()!='/')clean+='/';
  return clean+suffix;
}
inline std::string resolveHttpHref(const std::string& base,std::string_view href) {
  const HttpUrl parent=parseHttpUrl(base);
  if(!parent.valid || href.empty() || href.size()>1536 ||
     unsafeHttpString(href) || href.find('\\')!=std::string_view::npos ||
     href.substr(0,2)=="//")return {};
  HttpUrl child=parent;
  if(href.substr(0,7)=="http://") {
    child=parseHttpUrl(href);
    if(!child.valid || child.host!=parent.host || child.port!=parent.port)
      return {};
  } else {
    if(href.find("://")!=std::string_view::npos || href.front()=='#')
      return {};
    std::string relative(href);
    if(relative.front()=='/')child.target=relative;
    else if(relative.front()=='?') {
      child.target=parent.target.substr(0,parent.target.find('?'))+relative;
    } else {
      const auto basePath=parent.target.substr(0,parent.target.find('?'));
      const auto slash=basePath.rfind('/');
      child.target=basePath.substr(0,slash==std::string::npos?0:slash+1)+relative;
    }
  }
  child.target=normalizeHttpPath(child.target);
  if(child.target.empty())return {};
  return absoluteHttpUrl(child);
}

struct HttpResponse {
  int status=0;
  size_t bytes=0;
  std::string error;
  bool ok() const {return status>=200 && status<300 && error.empty();}
};
using HttpSink=std::function<bool(const char*,size_t)>;

class HttpSocketReader {
  int fd_=-1;
  char buf_[8192]{};
  size_t cursor_=0,count_=0;
  std::chrono::steady_clock::time_point deadline_;
public:
  explicit HttpSocketReader(int fd,unsigned seconds=20):fd_(fd),
      deadline_(std::chrono::steady_clock::now()+std::chrono::seconds(seconds)) {}
  bool fill() {
    if(cursor_<count_)return true;
    while(std::chrono::steady_clock::now()<deadline_) {
      const ssize_t n=::recv(fd_,buf_,sizeof(buf_),0);
      if(n>0) {cursor_=0;count_=static_cast<size_t>(n);return true;}
      if(n==0)return false;
      if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)continue;
      return false;
    }
    return false;
  }
  bool line(std::string& out,size_t limit=8192) {
    out.clear();
    while(out.size()<limit) {
      if(!fill())return false;
      const char ch=buf_[cursor_++];
      if(ch=='\n') {
        if(!out.empty()&&out.back()=='\r')out.pop_back();
        return true;
      }
      out.push_back(ch);
    }
    return false;
  }
  bool bytes(size_t wanted,const HttpSink& sink,size_t& consumed,
             size_t cap) {
    while(wanted) {
      if(!fill())return false;
      const size_t take=std::min(wanted,count_-cursor_);
      if(consumed>cap || take>cap-consumed ||
         !sink(buf_+cursor_,take))return false;
      cursor_+=take;consumed+=take;wanted-=take;
    }
    return true;
  }
  bool untilClose(const HttpSink& sink,size_t& consumed,size_t cap) {
    while(fill()) {
      const size_t take=count_-cursor_;
      if(consumed>cap||take>cap-consumed||
         !sink(buf_+cursor_,take))return false;
      consumed+=take;cursor_+=take;
    }
    return std::chrono::steady_clock::now()<deadline_;
  }
};

inline int connectHttpSocket(const HttpUrl& url) {
  addrinfo hints{};
  hints.ai_family=AF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  addrinfo* addresses=nullptr;
  if(::getaddrinfo(url.host.c_str(),url.port.c_str(),&hints,&addresses)!=0)
    return -1;
  int connected=-1;
  for(auto* ai=addresses;ai && connected<0;ai=ai->ai_next) {
    int fd=::socket(ai->ai_family,ai->ai_socktype,ai->ai_protocol);
    if(fd<0)continue;
    const int flags=fcntl(fd,F_GETFL,0);
    if(flags<0 || fcntl(fd,F_SETFL,flags|O_NONBLOCK)<0) {
      ::close(fd);continue;
    }
    int ret=::connect(fd,ai->ai_addr,ai->ai_addrlen);
    if(ret<0 && errno==EINPROGRESS) {
      pollfd probe{fd,POLLOUT,0};
      ret=::poll(&probe,1,3000);
      if(ret>0) {
        int err=0;socklen_t n=sizeof(err);
        ret=::getsockopt(fd,SOL_SOCKET,SO_ERROR,&err,&n)==0 && !err?0:-1;
      } else ret=-1;
    }
    if(ret==0) {
      (void)fcntl(fd,F_SETFL,flags);
      timeval timeout{3,0};
      (void)::setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
      (void)::setsockopt(fd,SOL_SOCKET,SO_SNDTIMEO,&timeout,sizeof(timeout));
      connected=fd;
    } else ::close(fd);
  }
  ::freeaddrinfo(addresses);
  return connected;
}
inline std::string lowercaseHttp(std::string value) {
  std::transform(value.begin(),value.end(),value.begin(),
     [](unsigned char c){return static_cast<char>(std::tolower(c));});
  return value;
}
inline HttpResponse httpRequest(const std::string& original,
                                 const HttpSink& sink,size_t maxBytes,
                                 const std::string& method="GET",
                                 const std::string& body={},
                                 const std::map<std::string,std::string>& extra={},
                                 unsigned redirect=0) {
  HttpResponse result;
  const auto url=parseHttpUrl(original);
  if(!url.valid || (method!="GET"&&method!="PUT"&&method!="POST") ||
     maxBytes==0 || maxBytes>64u*1024u*1024u ||
     body.size()>64u*1024u) {
    result.error="Invalid HTTP request (HTTPS not supported on this Q2 build)";
    return result;
  }
  for(const auto& [name,value]:extra)
    if(name.empty()||name.size()>64||unsafeHttpString(name)||
       value.size()>2048||unsafeHttpString(value)) {
      result.error="Unsafe HTTP header";return result;
    }
  int fd=connectHttpSocket(url);
  if(fd<0) {result.error="Connection unavailable";return result;}
  struct SocketGuard {int fd;~SocketGuard(){::close(fd);}} guard{fd};
  std::string request=method+" "+url.target+" HTTP/1.1\r\nHost: "+url.host+
      (url.port=="80"?"":":"+url.port)+
      "\r\nAccept: application/atom+xml, application/epub+zip, application/json\r\n"
      "Connection: close\r\nUser-Agent: CrossPod/0.7\r\n";
  for(const auto& [key,value]:extra)request+=key+": "+value+"\r\n";
  if(method!="GET") {
    request+="Content-Length: "+std::to_string(body.size())+"\r\n";
    if(!extra.count("Content-Type"))
      request+="Content-Type: application/json\r\n";
  }
  request+="\r\n";
  request+=body;
  for(size_t off=0;off<request.size();) {
    const ssize_t n=::send(fd,request.data()+off,request.size()-off,0);
    if(n<=0) {result.error="Send failed";return result;}
    off+=static_cast<size_t>(n);
  }
  HttpSocketReader input(fd);
  std::string line;
  if(!input.line(line,256) || line.substr(0,5)!="HTTP/") {
    result.error="Invalid HTTP response";return result;
  }
  const auto space=line.find(' ');
  if(space==std::string::npos||space+4>line.size()) {
    result.error="Invalid HTTP status";return result;
  }
  result.status=std::atoi(line.c_str()+space+1);
  std::map<std::string,std::string> headers;
  size_t headerBytes=0;
  while(input.line(line)) {
    headerBytes+=line.size()+2;
    if(headerBytes>16384) {result.error="Oversize headers";return result;}
    if(line.empty())break;
    const auto pos=line.find(':');
    if(pos==std::string::npos)continue;
    auto key=lowercaseHttp(line.substr(0,pos));
    std::string value=line.substr(pos+1);
    const auto first=value.find_first_not_of(" \t");
    if(first==std::string::npos)continue;
    value.erase(0,first);
    headers[key]=value;
  }
  if(result.status>=300&&result.status<400 && redirect<2 &&
     headers.count("location") && method=="GET") {
    const auto next=resolveHttpHref(original,headers["location"]);
    if(next.empty()) {result.error="Unsafe or HTTPS redirect";return result;}
    return httpRequest(next,sink,maxBytes,method,body,extra,redirect+1);
  }
  if(result.status<200||result.status>=300) {
    result.error="Server response "+std::to_string(result.status);return result;
  }
  if(method=="GET"&&result.status==204)return result;
  const auto transfer=lowercaseHttp(headers["transfer-encoding"]);
  const bool chunked=transfer.find("chunked")!=std::string::npos;
  if(chunked) {
    for(;;) {
      if(!input.line(line,128)) {result.error="Invalid chunk header";return result;}
      const size_t semicolon=line.find(';');
      const auto hex=line.substr(0,semicolon);
      if(hex.empty()||hex.size()>12) {result.error="Invalid chunk";return result;}
      size_t length=0;
      for(char c:hex) {
        unsigned digit=0;
        if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
        else if(c>='a'&&c<='f')digit=static_cast<unsigned>(c-'a'+10);
        else if(c>='A'&&c<='F')digit=static_cast<unsigned>(c-'A'+10);
        else {result.error="Invalid chunk size";return result;}
        if(length>(maxBytes-digit)/16) {
          result.error="Oversize chunk";return result;
        }
        length=length*16+digit;
      }
      if(length==0)break;
      if(!input.bytes(length,sink,result.bytes,maxBytes)) {
        result.error="Chunk transfer failed";return result;
      }
      if(!input.line(line,4)||!line.empty()) {
        result.error="Chunk delimiter invalid";return result;
      }
    }
  } else if(headers.count("content-length")) {
    const std::string& value=headers["content-length"];
    if(value.empty()||value.size()>16 ||
       value.find_first_not_of("0123456789")!=std::string::npos) {
      result.error="Invalid response length";return result;
    }
    // Clang's modern Linux headers redirect strtoull to __isoc23_strtoull,
    // absent from Q2's older stock glibc. Parse bounded decimal ourselves.
    size_t bytes=0;
    for(char c:value) {
      const size_t digit=static_cast<size_t>(c-'0');
      if(bytes>(maxBytes-digit)/10) {
        result.error="Download too large";return result;
      }
      bytes=bytes*10+digit;
    }
    if(!input.bytes(bytes,sink,result.bytes,maxBytes)) {
      result.error="Truncated response";return result;
    }
  } else if(!input.untilClose(sink,result.bytes,maxBytes)) {
    result.error="Connection ended unexpectedly";return result;
  }
  return result;
}
inline HttpResponse httpGetText(const std::string& url,
                                std::string& text,size_t max=512u*1024u,
                                const std::map<std::string,std::string>& headers={}) {
  text.clear();
  auto result=httpRequest(url,[&](const char* p,size_t size){
    if(text.size()>max||size>max-text.size())return false;
    text.append(p,size);return true;
  },max,"GET",{},headers);
  if(!result.ok())text.clear();
  return result;
}
} // namespace crosspod
