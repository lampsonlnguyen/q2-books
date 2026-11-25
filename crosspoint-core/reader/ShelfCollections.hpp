// SPDX-License-Identifier: MIT
#pragma once

#include "ShelfHistory.hpp"
#include "ShelfLibrary.hpp"
#include "ShelfIdentity.hpp"
#include "PortableMd5.hpp"
#include "UnicodeCase.hpp"
#include "ShelfSearch.hpp"
#include <map>
#include <set>
#include <sys/stat.h>

namespace books {
struct Collections {
  static constexpr size_t maxCollections=32,maxMemberships=4000,maxNameBytes=64;
  std::set<std::string> favorites;
  std::map<std::string,std::set<std::string>> groups;
  struct Identity {uint64_t size;std::string checksum;};
  std::map<std::string,Identity> identities;

  bool nameAvailable(const std::string& name,const std::string& except={}) const {
    if(name.empty() || name.size()>maxNameBytes || name.find_first_of("\n\r\t")!=std::string::npos ||
       name.find('\0')!=std::string::npos || name.find_first_not_of(' ')==std::string::npos)return false;
    const auto key=utf8Lower(foldShelfSearch(name));
    for(const auto& item:groups)
      if(item.first!=except && utf8Lower(foldShelfSearch(item.first))==key)return false;
    return true;
  }
};

inline std::string collectionsFile() {
  const auto history=recentBooksFile();
  return history.substr(0,history.find_last_of('/')+1)+"collections.conf";
}

inline Collections::Identity collectionIdentity(const std::string& path) {
  struct stat st{};
  if(lstat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) || st.st_size<=0)return {};
  const auto size=static_cast<uint64_t>(st.st_size);
  FILE* f=std::fopen(path.c_str(),"rb");
  if(!f)return {};
  unsigned char sample[1024];
  const size_t first=static_cast<size_t>(std::min<uint64_t>(size,sizeof(sample)));
  PortableMd5 hash;
  bool ok=std::fread(sample,1,first,f)==first;
  if(ok)hash.update(sample,first);
  if(ok && size>sizeof(sample)) {
    ok=::fseeko(f,st.st_size-static_cast<off_t>(sizeof(sample)),SEEK_SET)==0 &&
        std::fread(sample,1,sizeof(sample),f)==sizeof(sample);
    if(ok)hash.update(sample,sizeof(sample));
  }
  std::fclose(f);
  return ok?Collections::Identity{size,hash.finish()}:Collections::Identity{};
}

inline bool collectionsContain(const Collections& value,const std::string& path) {
  if(value.favorites.count(path))return true;
  return std::any_of(value.groups.begin(),value.groups.end(),
                     [&](const auto& group){return group.second.count(path)!=0;});
}

inline Collections loadCollections() {
  FILE* f=std::fopen(collectionsFile().c_str(),"rb");
  if(!f)return {};
  Collections value;
  char line[2200]{};
  const bool read=std::fgets(line,sizeof(line),f)!=nullptr;
  const bool v2=read && std::string_view(line)=="BOOKS-COLLECTIONS2\n";
  bool ok=v2 || (read && std::string_view(line)=="BOOKS-COLLECTIONS1\n");
  size_t memberships=0;
  std::set<std::string>* group=nullptr;
  while(ok && std::fgets(line,sizeof(line),f)) {
    std::string_view text(line);
    if(text.size()<3 || text[1]!=' ' || text.back()!='\n') {ok=false;break;}
    const char kind=text.front();
    const std::string field(text.substr(2,text.size()-3));
    if(kind=='C') {
      ok=value.groups.size()<Collections::maxCollections && value.nameAvailable(field);
      if(ok)group=&value.groups[field];
      continue;
    }
    std::string path;
    if(v2 && kind=='I') {
      const char* next=field.c_str();
      unsigned long long size=0;
      ok=value.identities.size()<Collections::maxMemberships &&
          parseIdentityNumber(next,size) && size && *next++==' ';
      if(!ok)break;
      const std::string_view rest(next);
      ok=rest.size()>33 && rest[32]==' ' &&
          rest.substr(0,32).find_first_not_of("0123456789abcdef")==std::string_view::npos &&
          parseHexBookPath(std::string(rest.substr(33)),path) &&
          value.identities.emplace(std::move(path),Collections::Identity{size,std::string(rest.substr(0,32))}).second;
      continue;
    }
    auto* target=kind=='F'?&value.favorites:kind=='B'?group:nullptr;
    ok=target && ++memberships<=Collections::maxMemberships &&
        parseHexBookPath(field,path) && target->insert(std::move(path)).second;
  }
  ok=ok && !std::ferror(f);
  std::fclose(f);
  return ok?std::move(value):Collections{};
}

inline bool saveCollections(const Collections& value) {
  if(value.groups.size()>Collections::maxCollections)return false;
  size_t total=value.favorites.size();
  for(const auto& group:value.groups) {
    total+=group.second.size();
    if(!value.nameAvailable(group.first,group.first))return false;
  }
  if(total>Collections::maxMemberships)return false;
  const auto path=collectionsFile();
  std::string temp=path+".tmp.XXXXXX";
  const int fd=mkstemp(temp.data());
  if(fd<0)return false;
  FILE* f=fdopen(fd,"wb");
  if(!f) {close(fd);std::remove(temp.c_str());return false;}
  bool ok=std::fputs("BOOKS-COLLECTIONS2\n",f)>=0;
  const auto writeBooks=[&](char kind,const std::set<std::string>& paths) {
    for(const auto& book:paths) {
      const auto encoded=hexBookPath(book);
      std::string decoded;
      ok=ok && parseHexBookPath(encoded,decoded) && decoded==book &&
          std::fprintf(f,"%c %s\n",kind,encoded.c_str())>0;
    }
  };
  writeBooks('F',value.favorites);
  for(const auto& group:value.groups) {
    ok=ok && std::fprintf(f,"C %s\n",group.first.c_str())>0;
    writeBooks('B',group.second);
  }
  for(const auto& item:value.identities) {
    if(!collectionsContain(value,item.first) || item.second.checksum.empty())continue;
    ok=ok && std::fprintf(f,"I %llu %s %s\n",static_cast<unsigned long long>(item.second.size),
                          item.second.checksum.c_str(),hexBookPath(item.first).c_str())>0;
  }
  ok=ok && std::fflush(f)==0 && fsync(fd)==0;
  if(std::fclose(f)!=0)ok=false;
  if(ok && std::rename(temp.c_str(),path.c_str())==0)return true;
  std::remove(temp.c_str());
  return false;
}
// Only move membership when there is exactly one matching local file.
inline bool restoreMovedCollectionBooks(Collections& value,const std::vector<ShelfEntry>& library) {
  if(value.identities.empty())return false;
  std::map<std::string_view,const ShelfEntry*> paths;
  for(const auto& book:library)paths.emplace(book.path,&book);
  std::map<uint64_t,std::vector<const ShelfEntry*>> sameSize;
  bool haveMissing=false;
  for(const auto& item:value.identities)
    if(!paths.count(item.first)) {sameSize.try_emplace(item.second.size);haveMissing=true;}
  if(!haveMissing)return false;
  for(const auto& book:library) {
    const auto candidates=sameSize.find(book.bytes);
    if(candidates!=sameSize.end())candidates->second.push_back(&book);
  }
  size_t remaining=128;
  std::map<std::string,Collections::Identity> checked;
  auto next=value;
  bool changed=false;
  for(const auto& item:value.identities) {
    const auto& oldPath=item.first;
    const auto& identity=item.second;
    if(paths.count(oldPath) || !collectionsContain(value,oldPath))continue;
    const auto possible=sameSize.find(identity.size);
    if(possible==sameSize.end() || possible->second.size()>32)continue;
    std::string match;
    bool unique=true;
    for(const auto* candidate:possible->second) {
      auto found=checked.find(candidate->path);
      if(found==checked.end()) {
        if(!remaining) {unique=false;break;}
        --remaining;
        found=checked.emplace(candidate->path,collectionIdentity(candidate->path)).first;
      }
      if(found->second.size!=identity.size || found->second.checksum!=identity.checksum)continue;
      if(!match.empty()) {unique=false;break;}
      match=candidate->path;
    }
    if(!unique || match.empty())continue;
    const auto migrate=[&](std::set<std::string>& list) {
      if(list.erase(oldPath))list.insert(match);
    };
    migrate(next.favorites);
    for(auto& group:next.groups)migrate(group.second);
    next.identities.erase(oldPath);
    next.identities[match]=identity;
    changed=true;
  }
  if(!changed)return false;
  (void)saveCollections(next);
  value=std::move(next);
  return true;
}
} // namespace books
