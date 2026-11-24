// SPDX-License-Identifier: MIT
#pragma once

#include "ConnectedHttp.hpp"
#include "UiLanguage.hpp"

#include <ContainerParser.h>
#include <ZipFile.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace books {
inline constexpr char userGuideLabel[]="userguide";
inline constexpr char userGuideTitle[]="Q2 Pod User Guide";
inline constexpr size_t userGuideMaxBytes=32u*1024u*1024u;

inline bool validUserGuideEpub(const std::string& path) {
  struct stat info{};
  if(lstat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode) ||
     info.st_size<32 || info.st_size>static_cast<off_t>(userGuideMaxBytes))return false;
  ZipFile zip(path);
  size_t size=0;
  constexpr char mime[]="application/epub+zip";
  if(!zip.getInflatedFileSize("mimetype",&size) || size!=sizeof(mime)-1)return false;
  const std::unique_ptr<uint8_t,decltype(&std::free)> type(
      zip.readFileToMemory("mimetype",&size,false),&std::free);
  if(!type || size!=sizeof(mime)-1 || std::memcmp(type.get(),mime,size)!=0)return false;
  constexpr char containerPath[]="META-INF/container.xml";
  if(!zip.getInflatedFileSize(containerPath,&size) || !size || size>256u*1024u)return false;
  ContainerParser container(size);
  return container.setup() && zip.readFileToStream(containerPath,container,512) &&
      !container.fullPath.empty() && zip.getInflatedFileSize(container.fullPath.c_str(),&size) && size>0;
}

// Preserve the cached EPUB's mtime when GitHub serves the same file, so the
// reader can reuse its existing pagination.
inline bool sameUserGuide(const std::string& cached,const std::string& fresh) {
  struct stat a{},b{};
  if(lstat(cached.c_str(),&a)!=0 || !S_ISREG(a.st_mode) ||
     stat(fresh.c_str(),&b)!=0 || a.st_size!=b.st_size)return false;
  FILE* oldFile=std::fopen(cached.c_str(),"rb");
  if(!oldFile)return false;
  FILE* newFile=std::fopen(fresh.c_str(),"rb");
  bool same=newFile!=nullptr;
  std::array<char,4096> oldBytes{},newBytes{};
  for(off_t left=a.st_size;same && left>0;) {
    const size_t count=static_cast<size_t>(std::min<off_t>(left,oldBytes.size()));
    same=std::fread(oldBytes.data(),1,count,oldFile)==count &&
        std::fread(newBytes.data(),1,count,newFile)==count &&
        std::memcmp(oldBytes.data(),newBytes.data(),count)==0;
    left-=static_cast<off_t>(count);
  }
  if(newFile)std::fclose(newFile);
  std::fclose(oldFile);
  return same;
}

inline HttpResponse downloadUserGuideEpub(const HttpSink& sink) {
  return httpsRequest(std::string("https://github.com")+q2PodGuideAssetPath,
      sink,userGuideMaxBytes,"GET",{},{},0,true);
}

// A fresh transfer on every open, with an offline fallback to the last
// validated EPUB. Keep the virtual title outside the scanned library.
inline std::string openUserGuide(std::string& error,
    HttpResponse (*download)(const HttpSink&)=downloadUserGuideEpub) {
  const auto dir=uiStateDir();
  const auto path=dir+"/Q2 Pod User Guide.epub";
  error.clear();
  const auto fallback=[&]() {return validUserGuideEpub(path)?path:std::string();};
  (void)mkdir(dir.c_str(),0755);
  std::string temporary=path+".part-XXXXXX";
  const int fd=mkstemp(temporary.data());
  if(fd<0) {
    error="Cannot save userguide";
    return fallback();
  }
  struct TempFile {
    const std::string& path;
    ~TempFile(){unlink(path.c_str());}
  } cleanup{temporary};
  FILE* file=fdopen(fd,"wb");
  if(!file) {
    close(fd);
    error="Cannot save userguide";
    return fallback();
  }
  const auto response=download([&](const char* data,size_t length) {
    return std::fwrite(data,1,length,file)==length;
  });
  const bool written=response.ok() && !httpAborted() &&
      std::fflush(file)==0 && fsync(fd)==0;
  const bool closed=std::fclose(file)==0;
  if(!response.ok())error="Userguide unavailable";
  else if(!written || !closed)error="Cannot save userguide";
  else if(!validUserGuideEpub(temporary))error="Invalid userguide";
  else if(sameUserGuide(path,temporary) || std::rename(temporary.c_str(),path.c_str())==0)
    return path;
  else error="Cannot save userguide";
  return fallback();
}
}  // namespace books
