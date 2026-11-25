// SPDX-License-Identifier: MIT
#pragma once

#include "ShelfDetails.hpp"
#include <array>
#include <charconv>
#include <cmath>
#include <memory>
#include <optional>
#include <string_view>

namespace books {
struct ShelfBookInfo {
  std::string title, author, publisher, language, description, identifier, isbn, asin, series, cover;
  std::optional<float> volume;
};

inline std::string shelfBookInfoFile(const std::string& path) {
  return positionFile(path)+".info";
}

inline bool parseShelfVolume(std::string_view text,std::optional<float>& volume) {
  volume.reset();
  if(text.empty())return true;
  float value=0;
  const auto result=std::from_chars(text.data(),text.data()+text.size(),value);
  if(result.ec!=std::errc{} || result.ptr!=text.data()+text.size() || !std::isfinite(value))return false;
  volume=value;
  return true;
}

inline bool loadShelfBookInfo(const std::string& path,ShelfBookInfo& info) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  std::unique_ptr<FILE,decltype(&std::fclose)> f(std::fopen(shelfBookInfoFile(path).c_str(),"rb"),&std::fclose);
  if(!f)return false;
  char header[128]{};
  unsigned long long bytes=0,modified=0;
  if(!std::fgets(header,sizeof(header),f.get()) ||
     !parseShelfIdentityHeader(header,"BOOKS-INFO1",bytes,modified) ||
     bytes!=static_cast<unsigned long long>(st.st_size) ||
     modified!=static_cast<unsigned long long>(st.st_mtime))return false;

  ShelfBookInfo value;
  const std::array<std::string*,10> fields={&value.title,&value.author,&value.publisher,
      &value.language,&value.description,&value.identifier,&value.isbn,&value.asin,&value.series,&value.cover};
  for(auto* field:fields) {
    const size_t limit=field==&value.description?4096:1024;
    size_t size=0;
    int c=std::fgetc(f.get());
    if(c<'0' || c>'9')return false;
    while(c>='0' && c<='9') {
      size=size*10+static_cast<size_t>(c-'0');
      if(size>limit)return false;
      c=std::fgetc(f.get());
      if(size==0 && c>='0' && c<='9')return false;
    }
    if(c!=':')return false;
    field->resize(size);
    if(std::fread(field->data(),1,size,f.get())!=size ||
       field->find('\0')!=std::string::npos || std::fgetc(f.get())!='\n')return false;
  }
  char volume[64]{};
  if(!std::fgets(volume,sizeof(volume),f.get()) || std::fgetc(f.get())!=EOF || std::ferror(f.get()))return false;
  std::string_view number(volume);
  if(number.empty() || number.back()!='\n')return false;
  number.remove_suffix(1);
  if(!parseShelfVolume(number,value.volume))return false;
  info=std::move(value);
  return true;
}

inline bool saveShelfBookInfo(const std::string& path,const ShelfBookInfo& info) {
  struct stat st{};
  if(::stat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  const auto file=shelfBookInfoFile(path),temp=file+".tmp";
  FILE* f=std::fopen(temp.c_str(),"wb");
  if(!f)return false;
  bool ok=std::fprintf(f,"BOOKS-INFO1 %llu %llu\n",static_cast<unsigned long long>(st.st_size),
      static_cast<unsigned long long>(st.st_mtime))>0;
  for(const auto* field:{&info.title,&info.author,&info.publisher,&info.language,&info.description,
                         &info.identifier,&info.isbn,&info.asin,&info.series,&info.cover}) {
    const size_t limit=field==&info.description?4096:1024;
    ok=ok && field->size()<=limit && std::fprintf(f,"%zu:",field->size())>0 &&
       std::fwrite(field->data(),1,field->size(),f)==field->size() && std::fputc('\n',f)!=EOF;
  }
  char volume[64]{};
  if(info.volume) {
    // Stock Q2 libstdc++ predates floating-point to_chars.
    const int written=std::snprintf(volume,sizeof(volume),"%.9g",static_cast<double>(*info.volume));
    ok=ok && std::isfinite(*info.volume) && written>0 && static_cast<size_t>(written)<sizeof(volume);
  }
  ok=ok && std::fprintf(f,"%s\n",volume)>0 && std::fflush(f)==0 && fsync(fileno(f))==0;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),file.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
} // namespace books
