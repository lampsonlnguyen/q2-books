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
inline constexpr char userGuideLabel[]="User Guide";
inline constexpr char userGuideSubtitle[]="Q2 Pod";
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

inline std::string cachedUserGuideEtag(const std::string& path) {
  struct stat info{};
  if(lstat(path.c_str(),&info)!=0 || !S_ISREG(info.st_mode))return {};
  FILE* file=std::fopen((path+".etag").c_str(),"rb");
  if(!file)return {};
  char stamp[64]{},tag[260]{};
  const bool read=std::fgets(stamp,sizeof(stamp),file) &&
      std::fgets(tag,sizeof(tag),file) && std::fgetc(file)==EOF;
  std::fclose(file);
  std::string value(tag);
  const auto expected=std::to_string(info.st_size)+" "+std::to_string(info.st_mtime)+"\n";
  if(!read || stamp!=expected || value.empty() || value.back()!='\n')return {};
  value.pop_back();
  return validHttpEtag(value)?value:std::string();
}

inline void saveUserGuideEtag(const std::string& path,const std::string& tag) {
  struct stat info{};
  if(!validHttpEtag(tag) || stat(path.c_str(),&info)!=0)return;
  const int fd=open((path+".etag").c_str(),O_WRONLY|O_CREAT|O_TRUNC|O_NOFOLLOW|O_CLOEXEC,0600);
  if(fd<0)return;
  if(FILE* file=fdopen(fd,"wb")) {
    std::fprintf(file,"%lld %lld\n%s\n",static_cast<long long>(info.st_size),
                 static_cast<long long>(info.st_mtime),tag.c_str());
    std::fclose(file);
  } else close(fd);
}

inline HttpResponse downloadUserGuideEpub(const HttpSink& sink,const std::string& etag) {
  std::map<std::string,std::string> headers;
  if(!etag.empty())headers.emplace("If-None-Match",etag);
  return httpsRequest(std::string("https://github.com")+q2PodGuideAssetPath,
      sink,userGuideMaxBytes,"GET",{},headers,0,true);
}

// Revalidate on open. A 304 response leaves the cached EPUB untouched.
inline std::string openUserGuide(std::string& error,
    HttpResponse (*download)(const HttpSink&,const std::string&)=downloadUserGuideEpub) {
  const auto dir=uiStateDir();
  const auto path=dir+"/Q2 Pod User Guide.epub";
  error.clear();
  const bool cached=validUserGuideEpub(path);
  const std::string etag=cached?cachedUserGuideEtag(path):std::string();
  std::string temporary;
  struct TempFile {
    const std::string& path;
    ~TempFile(){if(!path.empty())unlink(path.c_str());}
  } cleanup{temporary};
  FILE* file=nullptr;
  bool writeFailed=false;
  const auto response=download([&](const char* data,size_t length) {
    if(!file) {
      (void)mkdir(dir.c_str(),0755);
      std::string name=path+".part-XXXXXX";
      const int fd=mkstemp(name.data());
      if(fd<0) {writeFailed=true;return false;}
      temporary=std::move(name);
      file=fdopen(fd,"wb");
      if(!file) {close(fd);writeFailed=true;return false;}
    }
    writeFailed=std::fwrite(data,1,length,file)!=length;
    return !writeFailed;
  },etag);
  if(file) {
    if(response.ok() && (std::fflush(file)!=0 || fsync(fileno(file))!=0))writeFailed=true;
    if(std::fclose(file)!=0)writeFailed=true;
  }
  if(response.status==304 && response.error.empty() && !etag.empty())return path;
  if(writeFailed || httpAborted())error="Cannot save userguide";
  else if(!response.ok() || response.status!=200)error="Userguide unavailable";
  else if(!validUserGuideEpub(temporary))error="Invalid userguide";
  else if(cached && sameUserGuide(path,temporary)) {
    if(response.etag!=etag)saveUserGuideEtag(path,response.etag);
    return path;
  } else if((unlink((path+".etag").c_str())==0 || errno==ENOENT) &&
            std::rename(temporary.c_str(),path.c_str())==0) {
    saveUserGuideEtag(path,response.etag);
    return path;
  } else error="Cannot save userguide";
  return cached?path:std::string();
}
}  // namespace books
