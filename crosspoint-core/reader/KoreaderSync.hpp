// SPDX-License-Identifier: MIT
#pragma once
// Native KOReader sync protocol for Kavita's /api/koreader/<authkey> endpoint.
// Uses original CrossPoint XPath mapping (ProgressMapper) in Reader, not
// percentages converted to guessed pages. Sync is opt-in and manual.
#include "ConnectedConfig.hpp"
#include "PortableMd5.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <string_view>

namespace crosspod {
inline std::string jsonQuoted(const std::string& text) {
  std::string out="\"";
  constexpr char hex[]="0123456789abcdef";
  for(unsigned char c:text) {
    if(c=='"'||c=='\\') {out+='\\';out+=static_cast<char>(c);}
    else if(c=='\n')out+="\\n";
    else if(c=='\r')out+="\\r";
    else if(c=='\t')out+="\\t";
    else if(c<0x20) {
      out+="\\u00";out+=hex[c>>4];out+=hex[c&15];
    } else out+=static_cast<char>(c);
  }
  return out+"\"";
}
inline bool jsonStringValue(const std::string& json,const std::string& key,
                            std::string& value) {
  const std::string needle="\""+key+"\"";
  size_t start=json.find(needle);
  if(start==std::string::npos)return false;
  start=json.find(':',start+needle.size());
  if(start==std::string::npos)return false;
  start=json.find_first_not_of(" \t\r\n",start+1);
  if(start==std::string::npos || json[start]!='"')return false;
  ++start;
  std::string decoded;
  for(size_t i=start;i<json.size() && decoded.size()<4096;++i) {
    const char c=json[i];
    if(c=='"') {value=std::move(decoded);return true;}
    if(c=='\\') {
      if(++i>=json.size())return false;
      const char e=json[i];
      if(e=='"'||e=='\\'||e=='/')decoded+=e;
      else if(e=='n')decoded+='\n';
      else if(e=='r')decoded+='\r';
      else if(e=='t')decoded+='\t';
      else if(e=='u') {
        // XPath and stable document IDs are ASCII; reject unknown Unicode
        // escape sequences rather than silently corrupting a position.
        if(i+4>=json.size())return false;
        const std::string hex=json.substr(i+1,4);
        if(hex.find_first_not_of("0123456789abcdefABCDEF")!=std::string::npos)
          return false;
        const unsigned code=static_cast<unsigned>(
            std::strtoul(hex.c_str(),nullptr,16));
        if(code>127)return false;
        decoded+=static_cast<char>(code);
        i+=4;
      } else return false;
    } else if(static_cast<unsigned char>(c)<0x20)return false;
    else decoded+=c;
  }
  return false;
}
inline bool jsonFloatValue(const std::string& json,const std::string& key,
                           float& value) {
  const std::string needle="\""+key+"\"";
  const size_t keyStart=json.find(needle);
  if(keyStart==std::string::npos)return false;
  const auto colon=json.find(':',keyStart+needle.size());
  if(colon==std::string::npos)return false;
  const auto start=json.find_first_not_of(" \t\r\n",colon+1);
  if(start==std::string::npos)return false;
  char* end=nullptr;
  const double number=std::strtod(json.c_str()+start,&end);
  if(end==json.c_str()+start || !std::isfinite(number) ||
     number<0.0 || number>1.0)return false;
  value=static_cast<float>(number);
  return true;
}
struct RemoteProgress {
  std::string progress;  // original KOReader XPath
  float percentage=0;
  bool exists=false;
};
inline std::map<std::string,std::string> koReaderHeaders(
    const ConnectedConfig& cfg) {
  return {{"Accept","application/vnd.koreader.v1+json"},
          {"x-auth-user",cfg.username},
          {"x-auth-key",md5String(cfg.password)}};
}
inline bool validDocumentDigest(const std::string& hash) {
  if(hash.size()!=32)return false;
  return hash.find_first_not_of("0123456789abcdef")==std::string::npos;
}
inline bool pullKoreaderProgress(const ConnectedConfig& cfg,
                                 const std::string& document,
                                 RemoteProgress& remote,
                                 std::string& error) {
  remote={};error.clear();
  if(!cfg.syncConfigured()||!validDocumentDigest(document)) {
    error="Sync not configured";return false;
  }
  std::string url=cfg.syncUrl;
  if(url.back()=='/')url.pop_back();
  url+="/syncs/progress/"+document+"?position_kinds=locator,percentage";
  std::string payload;
  const auto result=httpGetText(url,payload,32768,koReaderHeaders(cfg));
  if(result.status==404 || result.status==204)return true;
  if(!result.ok()) {error=result.error;return false;}
  float percentage=0;
  std::string locator;
  if(!jsonFloatValue(payload,"percentage",percentage) ||
     !jsonStringValue(payload,"progress",locator))return true; // empty {}
  if(locator.size()>2048 || locator.find('\n')!=std::string::npos ||
     locator.find('\r')!=std::string::npos) {
    error="Invalid remote position";return false;
  }
  remote={locator,percentage,true};
  return true;
}
inline bool pushKoreaderProgress(const ConnectedConfig& cfg,
                                 const std::string& document,
                                 const RemoteProgress& local,
                                 std::string& error) {
  error.clear();
  if(!cfg.syncConfigured()||!validDocumentDigest(document) ||
     local.progress.empty() || local.progress.size()>2048 ||
     !std::isfinite(local.percentage) ||
     local.percentage<0 || local.percentage>1) {
    error="Invalid sync position";return false;
  }
  std::string url=cfg.syncUrl;
  if(url.back()=='/')url.pop_back();
  url+="/syncs/progress";
  char percentage[32]{};
  std::snprintf(percentage,sizeof(percentage),"%.7f",
                static_cast<double>(local.percentage));
  const std::string body="{\"document\":"+jsonQuoted(document)+
    ",\"progress\":"+jsonQuoted(local.progress)+
    ",\"percentage\":"+percentage+
    ",\"device\":\"CrossPod Q2\",\"device_id\":\"crosspod-q2\"}";
  auto headers=koReaderHeaders(cfg);
  headers["Content-Type"]="application/json";
  const auto response=httpRequest(url,[](const char*,size_t){return true;},
      32768,"PUT",body,headers);
  if(!response.ok()) {error=response.error;return false;}
  return true;
}
} // namespace crosspod
