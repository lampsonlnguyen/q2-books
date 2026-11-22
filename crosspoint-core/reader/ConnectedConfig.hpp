// SPDX-License-Identifier: MIT
#pragma once
// User-editable SD configuration. Never package real credentials or log URLs
// containing OPDS API tokens. Networking is opt-in, offline is always default.
//
// connected.conf holds the KOReader sync settings and up to kMaxOpdsServers
// OPDS catalogs. The first catalog may be written the old way (top-level
// opds_url=, with optional opds_username= / opds_password=); every catalog
// can also be a [server] block:
//
//   [server]
//   name=Calibre-Web
//   opds_url=https://books.example.org/opds
//   opds_username=lam
//   opds_password=secret
//
// Books rewrites the file (atomically, mode 0600) when servers are added,
// edited or deleted on the Q2; it then keeps the settings and drops comments.
#include "ReadingPosition.hpp"
#include "ConnectedHttp.hpp"
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <map>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace books {
inline constexpr size_t kMaxOpdsServers=8;
struct OpdsServer {
  std::string name;
  std::string url;
  std::string username;
  std::string password;
  bool valid() const {return parseHttpUrl(url).valid;}
};
struct ConnectedConfig {
  std::vector<OpdsServer> servers;  // valid catalogs, in file order
  std::string opdsUrl;              // the first catalog's URL (older callers)
  std::string syncUrl;
  std::string username;
  std::string password;
  bool enableSync=false;
  bool configured() const {return !servers.empty();}
  bool syncConfigured() const {
    return enableSync && parseHttpUrl(syncUrl).valid &&
      !username.empty() && !password.empty();
  }
};
inline std::string connectedConfigPath() {
  const std::string progress=positionFile("/__connected__");
  return progress.substr(0,progress.find_last_of('/'))+
         "/connected.conf";
}
inline std::string trimConnected(std::string text) {
  const auto first=text.find_first_not_of(" \t\r\n");
  if(first==std::string::npos)return {};
  const auto last=text.find_last_not_of(" \t\r\n");
  return text.substr(first,last-first+1);
}
// The name a server shows when it has none: its host.
inline std::string opdsServerLabel(const OpdsServer& server) {
  if(!server.name.empty())return server.name;
  const auto url=parseHttpUrl(server.url);
  return url.valid?url.host:std::string("Catalog");
}
inline bool sameHttpOrigin(const std::string& a,const std::string& b) {
  const auto x=parseHttpUrl(a),y=parseHttpUrl(b);
  return x.valid && y.valid && x.secure==y.secure && x.port==y.port &&
         lowercaseHttp(x.host)==lowercaseHttp(y.host);
}
inline std::string base64Encode(const std::string& text) {
  static const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  size_t i=0;
  for(;i+2<text.size();i+=3) {
    const unsigned v=(static_cast<unsigned char>(text[i])<<16)|
                     (static_cast<unsigned char>(text[i+1])<<8)|
                      static_cast<unsigned char>(text[i+2]);
    out+=table[v>>18];out+=table[(v>>12)&63];out+=table[(v>>6)&63];out+=table[v&63];
  }
  if(i<text.size()) {
    unsigned v=static_cast<unsigned char>(text[i])<<16;
    if(i+1<text.size())v|=static_cast<unsigned char>(text[i+1])<<8;
    out+=table[v>>18];out+=table[(v>>12)&63];
    out+= i+1<text.size()?table[(v>>6)&63]:'=';
    out+='=';
  }
  return out;
}
// The headers a request to |url| carries for |server|: HTTP Basic
// credentials go only to the catalog's own origin (scheme, host and port),
// never to a link that points at another host.
inline std::map<std::string,std::string> opdsHeadersFor(const OpdsServer& server,
                                                       const std::string& url) {
  if(server.username.empty() || !sameHttpOrigin(server.url,url))return {};
  return {{"Authorization","Basic "+base64Encode(server.username+":"+server.password)}};
}
inline bool connectedValueSafe(const std::string& value,size_t max) {
  if(value.size()>max)return false;
  for(unsigned char c:value)if(c<0x20||c==0x7f)return false;
  return true;
}
inline ConnectedConfig parseConnectedConfig(const std::string& text) {
  ConnectedConfig config;
  std::vector<OpdsServer> found;
  OpdsServer legacy;
  OpdsServer ignored;  // blocks past kMaxOpdsServers are read and dropped
  OpdsServer* current=&legacy;
  found.reserve(kMaxOpdsServers);
  size_t start=0;unsigned count=0;
  while(start<text.size() && count++<160) {
    size_t end=text.find('\n',start);
    if(end==std::string::npos)end=text.size();
    std::string setting=trimConnected(text.substr(start,end-start));
    start=end+1;
    if(setting.empty()||setting.front()=='#')continue;
    if(setting=="[server]") {
      if(found.size()<kMaxOpdsServers){found.push_back({});current=&found.back();}
      else {ignored={};current=&ignored;}
      continue;
    }
    const auto pos=setting.find('=');
    if(pos==std::string::npos)continue;
    const std::string key=trimConnected(setting.substr(0,pos));
    const std::string value=trimConnected(setting.substr(pos+1));
    if(key=="opds_url" && value.size()<=1536)current->url=value;
    else if(key=="name" && value.size()<=64)current->name=value;
    else if(key=="opds_username" && value.size()<=128)current->username=value;
    else if(key=="opds_password" && value.size()<=256)current->password=value;
    else if(current==&legacy) {
      if(key=="sync_url" && value.size()<=1536)config.syncUrl=value;
      else if(key=="sync_username" && value.size()<=128)config.username=value;
      else if(key=="sync_password" && value.size()<=256)config.password=value;
      else if(key=="enable_sync")config.enableSync=value=="true"||value=="1";
    }
  }
  if(legacy.valid())config.servers.push_back(std::move(legacy));
  for(auto& server:found)
    if(server.valid() && config.servers.size()<kMaxOpdsServers)
      config.servers.push_back(std::move(server));
  if(!config.servers.empty())config.opdsUrl=config.servers.front().url;
  if(!parseHttpUrl(config.syncUrl).valid)config.syncUrl.clear();
  return config;
}
inline ConnectedConfig loadConnectedConfig() {
  struct stat st{};
  const std::string file=connectedConfigPath();
  if(::lstat(file.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size>16384 || st.st_size<0)return {};
  FILE* input=std::fopen(file.c_str(),"rb");
  if(!input)return {};
  std::string text(static_cast<size_t>(st.st_size),'\0');
  const size_t got=std::fread(text.data(),1,text.size(),input);
  std::fclose(input);
  text.resize(got);
  return parseConnectedConfig(text);
}
inline std::string formatConnectedConfig(const ConnectedConfig& config) {
  std::string out="# Books connected settings. Books rewrites this file when you change\n"
                  "# servers on the Q2; see the README for every setting.\n";
  if(!config.syncUrl.empty())out+="sync_url="+config.syncUrl+"\n";
  if(!config.username.empty())out+="sync_username="+config.username+"\n";
  if(!config.password.empty())out+="sync_password="+config.password+"\n";
  out+=std::string("enable_sync=")+(config.enableSync?"true":"false")+"\n";
  for(const auto& server:config.servers) {
    out+="\n[server]\n";
    if(!server.name.empty())out+="name="+server.name+"\n";
    out+="opds_url="+server.url+"\n";
    if(!server.username.empty())out+="opds_username="+server.username+"\n";
    if(!server.password.empty())out+="opds_password="+server.password+"\n";
  }
  return out;
}
// Checks a server typed on the Q2. Empty when it can be saved, else why not.
inline std::string opdsServerProblem(const OpdsServer& server) {
  if(!connectedValueSafe(server.name,64) || !connectedValueSafe(server.username,128) ||
     !connectedValueSafe(server.password,256) || !connectedValueSafe(server.url,1536))
    return "Value too long";
  if(server.url.empty())return "Type the catalog URL";
  if(!server.valid())return "URL must start http:// or https://";
  if(server.username.empty() && !server.password.empty())return "Password needs a user";
  return {};
}
// Writes |config| to connected.conf: a new file, fsynced, then renamed over
// the old one, so a cut-off write never leaves half a file. Mode 0600: it
// holds passwords.
inline bool saveConnectedConfig(const ConnectedConfig& config,std::string& error) {
  error.clear();
  if(config.servers.size()>kMaxOpdsServers){error="Too many servers";return false;}
  for(const auto& server:config.servers)
    if(const auto problem=opdsServerProblem(server);!problem.empty()){error=problem;return false;}
  if(!connectedValueSafe(config.syncUrl,1536) || !connectedValueSafe(config.username,128) ||
     !connectedValueSafe(config.password,256)) {error="Value too long";return false;}
  const std::string path=connectedConfigPath(),temp=path+".tmp";
  const std::string text=formatConnectedConfig(config);
  const int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0){error="Cannot save settings";return false;}
  bool ok=true;
  for(size_t off=0;ok && off<text.size();) {
    const ssize_t n=::write(fd,text.data()+off,text.size()-off);
    if(n<=0)ok=false;else off+=static_cast<size_t>(n);
  }
  if(ok && ::fsync(fd)!=0)ok=false;
  if(::close(fd)!=0)ok=false;
  if(ok && ::rename(temp.c_str(),path.c_str())!=0)ok=false;
  if(!ok){::unlink(temp.c_str());error="Cannot save settings";}
  return ok;
}
} // namespace books
