// SPDX-License-Identifier: MIT
#pragma once
// User-editable SD configuration. Never package real credentials or log URLs
// containing OPDS API tokens. Networking is opt-in, offline is always default.
#include "ReadingPosition.hpp"
#include "ConnectedHttp.hpp"
#include <cstdio>
#include <string>
#include <sys/stat.h>

namespace books {
struct ConnectedConfig {
  std::string opdsUrl;
  std::string syncUrl;
  std::string username;
  std::string password;
  bool enableSync=false;
  bool configured() const {return parseHttpUrl(opdsUrl).valid;}
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
inline ConnectedConfig loadConnectedConfig() {
  ConnectedConfig config;
  struct stat st{};
  const std::string file=connectedConfigPath();
  if(::lstat(file.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size>8192 || st.st_size<0)return config;
  FILE* input=std::fopen(file.c_str(),"rb");
  if(!input)return config;
  char line[2048]{};
  unsigned count=0;
  while(count++<32 && std::fgets(line,sizeof(line),input)) {
    std::string setting(line);
    if(setting.find('\n')==std::string::npos && !std::feof(input))break;
    setting=trimConnected(setting);
    if(setting.empty()||setting.front()=='#')continue;
    const auto pos=setting.find('=');
    if(pos==std::string::npos)continue;
    const std::string key=trimConnected(setting.substr(0,pos));
    const std::string value=trimConnected(setting.substr(pos+1));
    if(key=="opds_url" && value.size()<=1536)config.opdsUrl=value;
    else if(key=="sync_url" && value.size()<=1536)config.syncUrl=value;
    else if(key=="sync_username" && value.size()<=128)config.username=value;
    else if(key=="sync_password" && value.size()<=256)config.password=value;
    else if(key=="enable_sync")config.enableSync=value=="true"||value=="1";
  }
  std::fclose(input);
  if(!parseHttpUrl(config.opdsUrl).valid)config.opdsUrl.clear();
  if(!parseHttpUrl(config.syncUrl).valid)config.syncUrl.clear();
  return config;
}
} // namespace books
