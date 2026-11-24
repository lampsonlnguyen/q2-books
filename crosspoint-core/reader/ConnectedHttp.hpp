// SPDX-License-Identifier: MIT
#pragma once
// Q2 POSIX HTTP/1.1 and verified HTTPS transport. The HTTPS backend uses
// Q2 Pod's existing libcurl and CA bundle; no ESP32 or firmware ownership.
#include <algorithm>
#include <cerrno>
#include <csignal>
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
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <vector>

namespace books {
// The reader points this at its SIGINT/SIGTERM flag. A transfer checks it at
// least every 250 ms (curl: every progress callback, about once a second or
// sooner) and stops: Q2 Pod SIGKILLs a card app 3 s after its SIGTERM, and a
// download used to run on for up to its whole timeout.
inline volatile std::sig_atomic_t* httpAbortFlag=nullptr;
inline bool httpAborted() { return httpAbortFlag && *httpAbortFlag; }
// A transfer fails when it makes no progress for this long, not when a whole
// download takes long: a large EPUB on a slow link is not a stalled one.
constexpr unsigned httpStallSeconds=30;
constexpr unsigned httpTotalSeconds=30*60;

struct HttpUrl {
  std::string host;
  std::string port="80";
  std::string target="/";
  bool secure=false;
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
  if(text.size()>1536 || unsafeHttpString(text))return url;
  if(text.substr(0,8)=="https://") {
    url.secure=true;
    url.port="443";
    text.remove_prefix(8);
  } else if(text.substr(0,7)=="http://")text.remove_prefix(7);
  else return url;
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
  return std::string(url.secure?"https://":"http://")+url.host+
         (url.port==(url.secure?"443":"80")?"":":"+url.port)+url.target;
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
  if(href.substr(0,7)=="http://" || href.substr(0,8)=="https://") {
    child=parseHttpUrl(href);
    if(!child.valid || child.host!=parent.host || child.port!=parent.port ||
       child.secure!=parent.secure)return {};
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

// The stock Shanling Q2 runtime ships libcurl with TLS and a CA bundle.
// Use the ABI exposed by Q2 Pod itself; the MIPS sysroot has no curl headers.
// Numeric options are the stable libcurl easy option values (libcurl 7.x).
extern "C" {
struct curl_slist;
void* curl_easy_init(void);
int curl_easy_setopt(void*,int,...);
int curl_easy_perform(void*);
int curl_easy_getinfo(void*,int,...);
void curl_easy_cleanup(void*);
curl_slist* curl_slist_append(curl_slist*,const char*);
void curl_slist_free_all(curl_slist*);
}
struct HttpsStream {
  const HttpSink* sink;
  size_t maxBytes;
  size_t count=0;
  int status=0;
  std::string location;
  bool aborted=false;
};
inline size_t httpsBodyCallback(char* bytes,size_t unit,size_t count,void* context) {
  auto& state=*static_cast<HttpsStream*>(context);
  if(unit && count>std::numeric_limits<size_t>::max()/unit)return 0;
  const size_t length=unit*count;
  if(state.status>=300 && state.status<400)return length; // never write redirect body
  if(state.status<200 || state.status>=300)return length; // never write error page to EPUB
  if(state.count>state.maxBytes || length>state.maxBytes-state.count ||
     !(*state.sink)(bytes,length)) {
    state.aborted=true;
    return 0;
  }
  state.count+=length;
  return length;
}
inline int httpsProgressCallback(void*,long long,long long,long long,long long) {
  return httpAborted()?1:0;  // non-zero: CURLE_ABORTED_BY_CALLBACK
}
inline size_t httpsHeaderCallback(char* bytes,size_t unit,size_t count,void* context) {
  auto& state=*static_cast<HttpsStream*>(context);
  if(unit && count>std::numeric_limits<size_t>::max()/unit)return 0;
  const size_t length=unit*count;
  if(length>8192)return 0;
  std::string_view line(bytes,length);
  if(line.substr(0,5)=="HTTP/") {
    const auto space=line.find(' ');
    state.status=space!=std::string_view::npos && space+4<=line.size() &&
        line[space+1]>='0' && line[space+1]<='9' &&
        line[space+2]>='0' && line[space+2]<='9' &&
        line[space+3]>='0' && line[space+3]<='9' ?
        (line[space+1]-'0')*100+(line[space+2]-'0')*10+line[space+3]-'0':0;
    state.location.clear();
  } else if(line.size()>=9) {
    std::string prefix(line.substr(0,9));
    std::transform(prefix.begin(),prefix.end(),prefix.begin(),[](unsigned char c){
      return static_cast<char>(std::tolower(c));});
    if(prefix=="location:") {
      line.remove_prefix(9);
      const auto begin=line.find_first_not_of(" \t");
      if(begin!=std::string_view::npos) {
        line.remove_prefix(begin);
        const auto end=line.find_first_of("\r\n");
        line=line.substr(0,end);
        if(line.size()<=1536 && !unsafeHttpString(line))
          state.location=std::string(line);
      }
    }
  }
  return length;
}
inline std::string httpsCaBundle() {
  const char* state=std::getenv("BOOKS_STATE_DIR");
  const std::string card=std::string(state&&*state?state:"/mnt/mmc/.books")+"/ca-bundle.pem";
  struct stat info{};
  if(::stat(card.c_str(),&info)==0 && S_ISREG(info.st_mode) && info.st_size>1024 &&
     ::access(card.c_str(),R_OK)==0)return card;
  return "/etc/scrobble-ca.pem";
}
// GitHub release assets redirect to signed, expiring CDN URLs. This
// exception is opt-in for public font packs only; OPDS/sync requests keep
// their same-origin rule and credentials are never sent across origins.
inline std::string githubFontRedirect(const std::string& from,const std::string& location) {
  const auto source=parseHttpUrl(from), target=parseHttpUrl(location);
  const auto cdn=[](const std::string& host) {
    return host=="release-assets.githubusercontent.com" ||
           host=="objects.githubusercontent.com" ||
           host=="github-releases.githubusercontent.com";
  };
  if(!source.valid || !source.secure || source.port!="443" ||
     !target.valid || !target.secure || target.port!="443" || !cdn(target.host))return {};
  const std::string prefix="/lampsonlnguyen/q2-books-fonts/releases/download/";
  if(!(source.host=="github.com" && source.target.rfind(prefix,0)==0) && !cdn(source.host))
    return {};
  return location;
}
inline HttpResponse httpsRequest(const std::string& original,const HttpSink& sink,
                                 size_t maxBytes,const std::string& method,
                                 const std::string& body,
                                 const std::map<std::string,std::string>& extra,
                                 unsigned redirect=0,bool fontRelease=false) {
  HttpResponse result;
  if(redirect>2 || !parseHttpUrl(original).secure) {
    result.error="Invalid HTTPS request";return result;
  }
  void* curl=curl_easy_init();
  if(!curl) {result.error="HTTPS unavailable";return result;}
  struct Guard {
    void* curl;
    curl_slist* headers=nullptr;
    ~Guard() {
      if(headers)curl_slist_free_all(headers);
      curl_easy_cleanup(curl);
    }
  } guard{curl};
  HttpsStream stream{&sink,maxBytes};
  constexpr int urlOpt=10002, agentOpt=10018, writeOpt=20011,
      writeDataOpt=10001, headerOpt=20079, headerDataOpt=10029,
      headerListOpt=10023, timeoutOpt=13, connectTimeoutOpt=78,
      noSignalOpt=99, followOpt=52, caOpt=10065, peerOpt=64, hostOpt=81,
      protocolsOpt=181, redirectProtocolsOpt=182, customMethodOpt=10036,
      bodyOpt=10015, bodySizeOpt=60, progressOpt=20219, noProgressOpt=43,
      lowSpeedLimitOpt=19, lowSpeedTimeOpt=20;
  constexpr long httpsOnly=2L; // CURLPROTO_HTTPS
  bool settings=
      curl_easy_setopt(curl,urlOpt,original.c_str())==0 &&
      curl_easy_setopt(curl,agentOpt,"Books/0.9") ==0 &&
      curl_easy_setopt(curl,writeOpt,httpsBodyCallback)==0 &&
      curl_easy_setopt(curl,writeDataOpt,&stream)==0 &&
      curl_easy_setopt(curl,headerOpt,httpsHeaderCallback)==0 &&
      curl_easy_setopt(curl,headerDataOpt,&stream)==0 &&
      curl_easy_setopt(curl,timeoutOpt,static_cast<long>(httpTotalSeconds))==0 &&
      curl_easy_setopt(curl,lowSpeedLimitOpt,1024L)==0 &&  // under 1 KB/s ...
      curl_easy_setopt(curl,lowSpeedTimeOpt,static_cast<long>(httpStallSeconds))==0 &&  // ... for 30 s
      curl_easy_setopt(curl,progressOpt,httpsProgressCallback)==0 &&
      curl_easy_setopt(curl,noProgressOpt,0L)==0 &&
      curl_easy_setopt(curl,connectTimeoutOpt,10L)==0 &&
      curl_easy_setopt(curl,noSignalOpt,1L)==0 &&
      curl_easy_setopt(curl,followOpt,0L)==0 &&
      curl_easy_setopt(curl,protocolsOpt,httpsOnly)==0 &&
      curl_easy_setopt(curl,redirectProtocolsOpt,httpsOnly)==0 &&
      curl_easy_setopt(curl,peerOpt,1L)==0 &&
      curl_easy_setopt(curl,hostOpt,2L)==0;
#ifdef BOOKS_Q2_RUNTIME
  // Q2 Pod's /etc/scrobble-ca.pem holds only the 8 roots its scrobbler needs
  // (no Sectigo/ZeroSSL RSA, DigiCert G1, Amazon or SSL.com: Cloudflare's edge
  // certificates come from SSL.com too). The card ships Mozilla's root list as
  // .books/ca-bundle.pem, beside the reader binary it already trusts; that
  // bundle is used when present, Q2 Pod's file otherwise. Verification itself
  // is never relaxed, and curl fails closed if the file is unusable.
  settings=settings && curl_easy_setopt(curl,caOpt,httpsCaBundle().c_str())==0;
#else
  // Only host tests can substitute a certificate authority. Never compiled
  // into the device binary (which always uses the firmware-trusted roots).
  const char* testCa=std::getenv("BOOKS_TEST_CA_BUNDLE");
  if(testCa && *testCa)settings=settings && curl_easy_setopt(curl,caOpt,testCa)==0;
#endif
  for(const auto& [name,value]:extra) {
    for(unsigned char ch:name)
      if(!(std::isalnum(ch)||ch=='-')) {
        result.error="Invalid HTTPS header name";return result;
      }
    const std::string header=name+": "+value;
    curl_slist* updated=curl_slist_append(guard.headers,header.c_str());
    if(!updated) {result.error="Out of memory";return result;}
    guard.headers=updated;
  }
  if(method!="GET") {
    const std::string contentType="Content-Type: application/json";
    if(!extra.count("Content-Type")) {
      curl_slist* updated=curl_slist_append(guard.headers,contentType.c_str());
      if(!updated) {result.error="Out of memory";return result;}
      guard.headers=updated;
    }
    settings=settings &&
        curl_easy_setopt(curl,customMethodOpt,method.c_str())==0 &&
        curl_easy_setopt(curl,bodyOpt,body.c_str())==0 &&
        curl_easy_setopt(curl,bodySizeOpt,static_cast<long>(body.size()))==0;
  }
  if(guard.headers)
    settings=settings && curl_easy_setopt(curl,headerListOpt,guard.headers)==0;
  if(!settings) {result.error="HTTPS transport setup failed";return result;}
  const int code=curl_easy_perform(curl);
  long status=0;
  if(code==0)(void)curl_easy_getinfo(curl,0x200002,&status);
  result.status=static_cast<int>(status);
  result.bytes=stream.count;
  if(code!=0) {
    result.error=code==42?"Transfer stopped":   // CURLE_ABORTED_BY_CALLBACK
        stream.aborted?"HTTPS download too large or rejected":
        code==28?"HTTPS transfer stalled":       // CURLE_OPERATION_TIMEDOUT
        code==60?"HTTPS certificate not trusted (check the device clock)":
        "HTTPS transfer failed ("+std::to_string(code)+")";
    return result;
  }
  if(status>=300 && status<400) {
    if(method=="GET" && redirect<2 && !stream.location.empty()) {
      auto next=resolveHttpHref(original,stream.location);
      if(next.empty() && fontRelease && extra.empty())
        next=githubFontRedirect(original,stream.location);
      if(!next.empty() && parseHttpUrl(next).secure)
        return httpsRequest(next,sink,maxBytes,method,body,extra,redirect+1,fontRelease);
    }
    result.error="HTTPS redirect refused (origin or scheme changed)";
  } else if(status<200 || status>=300)
    result.error="Server response "+std::to_string(status);
  return result;
}

class HttpSocketReader {
  int fd_=-1;
  char buf_[8192]{};
  size_t cursor_=0,count_=0;
  std::chrono::steady_clock::time_point deadline_,stall_;
  unsigned stallSeconds_;
public:
  bool timedOut=false;
  explicit HttpSocketReader(int fd,unsigned stallSeconds=httpStallSeconds,
                            unsigned totalSeconds=httpTotalSeconds):fd_(fd),
      deadline_(std::chrono::steady_clock::now()+std::chrono::seconds(totalSeconds)),
      stall_(std::chrono::steady_clock::now()+std::chrono::seconds(stallSeconds)),
      stallSeconds_(stallSeconds) {}
  bool fill() {
    if(cursor_<count_)return true;
    for(;;) {
      const auto now=std::chrono::steady_clock::now();
      if(now>=deadline_ || now>=stall_) {timedOut=true;return false;}
      if(httpAborted())return false;
      pollfd ready{fd_,POLLIN,0};
      const int polled=::poll(&ready,1,250);
      if(polled<0 && errno!=EINTR)return false;
      if(polled<=0)continue;
      const ssize_t n=::recv(fd_,buf_,sizeof(buf_),MSG_DONTWAIT);
      if(n>0) {
        cursor_=0;count_=static_cast<size_t>(n);
        stall_=std::chrono::steady_clock::now()+std::chrono::seconds(stallSeconds_);
        return true;
      }
      if(n==0)return false;
      if(errno==EINTR || errno==EAGAIN || errno==EWOULDBLOCK)continue;
      return false;
    }
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
    return !timedOut && !httpAborted();
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
      ret=0;
      for(int waited=0;ret==0 && waited<3000 && !httpAborted();waited+=250)
        ret=::poll(&probe,1,250);  // 3 s, in slices a SIGTERM can stop
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
    result.error="Invalid HTTP request";
    return result;
  }
  for(const auto& [name,value]:extra)
    if(name.empty()||name.size()>64||unsafeHttpString(name)||
       value.size()>2048||unsafeHttpString(value)) {
      result.error="Unsafe HTTP header";return result;
    }
  if(url.secure)
    return httpsRequest(original,sink,maxBytes,method,body,extra,redirect);
  int fd=connectHttpSocket(url);
  if(fd<0) {result.error="Connection unavailable";return result;}
  struct SocketGuard {int fd;~SocketGuard(){::close(fd);}} guard{fd};
  std::string request=method+" "+url.target+" HTTP/1.1\r\nHost: "+url.host+
      (url.port=="80"?"":":"+url.port)+
      "\r\nAccept: application/atom+xml, application/epub+zip, application/json\r\n"
      "Connection: close\r\nUser-Agent: Books/0.9\r\n";
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
} // namespace books
