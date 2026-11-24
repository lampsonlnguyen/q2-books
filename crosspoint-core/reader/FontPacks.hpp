// SPDX-License-Identifier: MIT
#pragma once
// Optional font packs from our versioned public release. The compiled
// catalog pins both size and SHA-256 from the published GitHub asset metadata.
// Download/extract streams to SD, never holds an entire pack in memory.
#include "ConnectedHttp.hpp"
#include "FontPackSha256.hpp"
#include "Typography.hpp"
#include <ZipFile.h>
#include <Print.h>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace books {
struct FontPack {
  const char* slug;
  const char* label;
  size_t bytes;
  const char* sha256;
  bool smooth;
};
inline constexpr const char* fontPackRelease="fonts-v1.0.0";
inline constexpr FontPack fontPacks[]={
  {"serif", "Noto Serif", 2713384u, "ce959800533f699493d83d72d84eaf6b07a833be1ed640cc8caef102d462eeb3", true},
  {"bitter", "Bitter", 1611283u, "03def11a826f61567cfed60b49c2e724742c454070d20b3fbedd3dedd052555f", false},
  {"lexenddeca", "Lexend Deca", 1247334u, "e7d6ab71550d23b3fbea41e4fbd18d48b5e8f10b5872b7c8123a32ab24f00a61", true},
  {"chareink", "ChareInk", 3232356u, "5d5234a79cbff42da2888249745424f2b23aed3ae69394e23dff8b8e7517e907", true},
  {"literata", "Literata", 2543117u, "1f1f58b3b7f4d8ab13e0182e370d98e53e832ebf51d347e1adc5ccedac3764c5", true},
  {"merriweather", "Merriweather", 2086012u, "e1f8204617d2c31214d3b406a9eb2f051797649c621fedd2f72f37345a8ca8a2", true},
  {"inter", "Inter", 2759908u, "28e9e5bfd94672803272f344b3ebab49a6b2836795717e1027c645417be70013", true},
  {"atkinson", "Atkinson Hyperlegible", 1192730u, "4d09a04ebebc05f5c6454df9cf2eb6db88d1bf30768d172b2772f988529024d3", true},
  {"cjk", "Chinese / Japanese", 15207744u, "7c12c3aaac53386e66acd1e8593593b15f5424cbfed3fa8cab18ef49e2457649", false},
  {"korean", "Korean", 6207252u, "86a5002f7cdd03c906a32cf9d5249749846cefa5e9dff11468e0b4717963fbe5", false},
  {"devanagari", "Hindi / Marathi / Nepali", 611645u, "02cdfbae0d318174033bb94b2d6cf737c54047c4dba763c5c7aa8cf8bfe4176c", false},
  {"bengali", "Bengali / Assamese", 586056u, "def3418883f17214754fe2934bcd8f619b64623e21dd8eb15ea67d2c6fcc8d1c", false},
  {"gujarati", "Gujarati", 583466u, "ef21d2f296469a613248bf135c8653e474a3ba47e1b7bcc57de8ea3d657d8d52", false},
  {"gurmukhi", "Punjabi", 510726u, "e82d8f0472bb6668ba271aeaa2f94cf5dd391c7c044322a7f9e74ec582a4cd1f", false},
  {"kannada", "Kannada", 586015u, "7707b9e0c452a0416a4f5b38ca64c6f5dc2bcea2fa1c0515be328cafe51a789e", false},
  {"malayalam", "Malayalam", 602557u, "5703815d232dad0f2c69bab150fb6961f64de41b95686653628e20880889eb2d", false},
  {"oriya", "Odia", 583221u, "9cf80de9217f172fde8af4092f3fe8db5c2d45e12d72b5f91b3fe6aa3cd9cf39", false},
  {"sinhala", "Sinhala", 570657u, "b67a1825cf161713fce3367da0233855105e33f39bac9c853b9fc0e3ba3fd890", false},
  {"tamil", "Tamil", 554926u, "f7691b90e611739c6bac5fa927c2bec0bcbb9ef9a0fe9d364c59fb49ea44ee7d", false},
  {"telugu", "Telugu", 603130u, "684945bc1bae839656a0a16d67f64f471b69e8c97e1e580bc67554f4b690e14c", false},
  {"arabic", "Arabic", 605959u, "60b8ea88e58fe46b4e0d941433f3c89fcc75cde2b88462c87af49c445049e2ba", false},
  {"hebrew", "Hebrew", 793194u, "1e7cdb6ee4ec5eb09ced0f31a4041a6ae6920b7ef3cc3762a4d2610831b7ff0c", false},
};
inline constexpr size_t fontPackCount=sizeof(fontPacks)/sizeof(fontPacks[0]);

inline bool fontPackDirectory(const std::string& path) {
  struct stat st{};
  if(::mkdir(path.c_str(),0755)!=0 && errno!=EEXIST)return false;
  return ::lstat(path.c_str(),&st)==0 && S_ISDIR(st.st_mode);
}
inline std::string fontPackRoot() {return typographyRoot()+"/fonts";}
inline bool fontPackTtf(const std::string& path) {
  struct stat st{};
  if(::lstat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode) ||
     st.st_size<64 || st.st_size>kUserFontMaxBytes)return false;
  FILE* f=std::fopen(path.c_str(),"rb");
  if(!f)return false;
  unsigned char magic[4]{};
  const bool read=std::fread(magic,1,4,f)==4;
  std::fclose(f);
  return read && (std::memcmp(magic,"\x00\x01\x00\x00",4)==0 ||
         std::memcmp(magic,"OTTO",4)==0 || std::memcmp(magic,"true",4)==0);
}
inline bool fontPackInstalled(const FontPack& pack) {
  const auto root=fontPackRoot()+"/";
  for(int pt=8;pt<=14;++pt)
    if(!usableCpfont(root+pack.slug+"-"+std::to_string(pt)+".cpfont"))return false;
  return !pack.smooth || fontPackTtf(root+"ttf/"+pack.slug+"-regular.ttf");
}
// Exact optional-pack filenames only. Never remove the core or user Fonts/.
inline bool optionalFontSlug(const std::string& slug) {
  for(const auto& pack:fontPacks)if(slug==pack.slug)return true;
  return false;
}
inline bool fontPackHasFiles(const FontPack& pack) {
  if(!optionalFontSlug(pack.slug))return false;
  const auto root=fontPackRoot()+"/";
  struct stat st{};
  for(int pt=8;pt<=14;++pt)
    if(::lstat((root+pack.slug+"-"+std::to_string(pt)+".cpfont").c_str(),&st)==0)return true;
  for(const char* style:{"regular","bold","italic","bolditalic"})
    if(::lstat((root+"ttf/"+pack.slug+"-"+style+".ttf").c_str(),&st)==0)return true;
  return false;
}
inline bool removeFontPack(const FontPack& pack) {
  if(!optionalFontSlug(pack.slug))return false;
  const auto root=fontPackRoot();
  struct stat st{};
  if(::lstat(root.c_str(),&st)!=0 || !S_ISDIR(st.st_mode))return false;
  // Refuse to follow directory symlinks.
  for(const char* sub:{"ttf","licenses"}) {
    const auto dir=root+"/"+sub;
    if(::lstat(dir.c_str(),&st)==0) {
      if(!S_ISDIR(st.st_mode))return false;
    } else if(errno!=ENOENT)return false;
  }
  Typography saved;
  if(loadTypography(saved)) {
    const char* slug=typographyFamilySlug(saved.family);
    if(slug && std::strcmp(slug,pack.slug)==0 &&
       !saveTypography({Typography::Family::Sans,saved.size}))return false;
  }
  bool ok=true;
  const auto remove=[&](const std::string& name) {
    if(::unlink((root+"/"+name).c_str())!=0 && errno!=ENOENT)ok=false;
  };
  for(int pt=8;pt<=14;++pt)
    remove(std::string(pack.slug)+"-"+std::to_string(pt)+".cpfont");
  for(const char* style:{"regular","bold","italic","bolditalic"})
    remove(std::string("ttf/")+pack.slug+"-"+style+".ttf");
  remove(std::string("licenses/")+pack.slug+".txt");
  return ok;
}
inline bool fontPackPath(const FontPack& pack,const std::string& path) {
  const std::string slug=pack.slug;
  for(int pt=8;pt<=14;++pt)
    if(path==slug+"-"+std::to_string(pt)+".cpfont")return true;
  if(pack.smooth)
    for(const char* style:{"regular","bold","italic","bolditalic"})
      if(path=="ttf/"+slug+"-"+style+".ttf")return true;
  return path=="licenses/"+slug+".txt";
}
inline bool fontPackFileValid(const std::string& path,const std::string& name) {
  struct stat st{};
  if(::lstat(path.c_str(),&st)!=0 || !S_ISREG(st.st_mode))return false;
  if(name.rfind("ttf/",0)==0)return fontPackTtf(path);
  if(name.rfind("licenses/",0)==0)return st.st_size>0 && st.st_size<=1024*1024;
  return usableCpfont(path);
}
struct FontPackStage {
  std::string root;
  std::vector<std::string> files;
  ~FontPackStage() {
    for(const auto& file:files)(void)::unlink((root+"/"+file).c_str());
    (void)::rmdir((root+"/ttf").c_str());
    (void)::rmdir((root+"/licenses").c_str());
    (void)::rmdir(root.c_str());
  }
};
class FontPackSink final : public Print {
  FILE* file_;
  size_t remaining_;
 public:
  FontPackSink(FILE* file,size_t size):file_(file),remaining_(size){}
  size_t write(uint8_t byte) override {return write(&byte,1);}
  size_t write(const uint8_t* data,size_t size) override {
    if(httpAborted() || size>remaining_)return 0;
    const size_t wrote=std::fwrite(data,1,size,file_);
    remaining_-=wrote;
    return wrote;
  }
  bool complete() const {return remaining_==0;}
};
// Validate every entry before exposing any files. Extract only exact names
// belonging to this pack, not arbitrary archive paths. Existing regular valid
// font files are retained (including files installed by older app versions).
inline bool installVerifiedFontArchive(const FontPack& pack,FontPackStage& stage,
                                       const std::string& archive,std::string& error) {
  const auto root=fontPackRoot();
  ZipFile zip(archive);
  std::set<std::string> names;
  bool valid=true;
  if(!zip.enumerateFilePaths([&](std::string_view entry) {
       const std::string name(entry);
       if(!fontPackPath(pack,name) || names.size()>=12 || !names.insert(name).second)
         valid=false;
     }) || !valid) {error="Invalid font pack";return false;}
  for(int pt=8;pt<=14;++pt)
    if(!names.count(std::string(pack.slug)+"-"+std::to_string(pt)+".cpfont"))valid=false;
  if(!names.count(std::string("licenses/")+pack.slug+".txt"))valid=false;
  if(pack.smooth && !names.count(std::string("ttf/")+pack.slug+"-regular.ttf"))valid=false;
  if(!valid){error="Incomplete font pack";return false;}
  size_t total=0;
  for(const auto& name:names) {
    size_t size=0;
    const size_t cap=name.rfind("licenses/",0)==0?1024u*1024u:16u*1024u*1024u;
    if(httpAborted() || !zip.getInflatedFileSize(name.c_str(),&size) ||
       size==0 || size>cap || size>128u*1024u*1024u-total) {
      error="Invalid font pack size";return false;
    }
    total+=size;
    const std::string output=stage.root+"/"+name;
    stage.files.push_back(name);
    FILE* f=std::fopen(output.c_str(),"wb");
    if(!f){error="Cannot save font";return false;}
    FontPackSink sink(f,size);
    bool ok=zip.readFileToStream(name.c_str(),sink,16384) && sink.complete();
    if(std::fflush(f)!=0)ok=false;
    if(ok && ::fsync(fileno(f))!=0)ok=false;
    if(std::fclose(f)!=0)ok=false;
    if(!ok || !fontPackFileValid(output,name)) {error="Invalid font data";return false;}
    struct stat existing{};
    const auto target=root+"/"+name;
    if(::lstat(target.c_str(),&existing)==0 && !fontPackFileValid(target,name)) {
      // Never replace an unrecognized existing file; manual removal is needed.
      error="Existing font needs repair";return false;
    }
  }
  if(httpAborted()){error="Transfer stopped";return false;}
  // Publish only after complete validation; rename is atomic per file.
  // Install licenses and smooth faces first, then bitmap files. A power cut
  // leaves at most a valid partial pack which can be completed on retry.
  std::vector<std::string> ordered(names.begin(),names.end());
  std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b) {
    return (a.find('/')!=std::string::npos)>(b.find('/')!=std::string::npos);
  });
  for(const auto& name:ordered) {
    const auto target=root+"/"+name;
    struct stat existing{};
    if(::lstat(target.c_str(),&existing)==0) {
      if(!fontPackFileValid(target,name)){error="Existing font needs repair";return false;}
      continue;
    }
    if(httpAborted() || ::rename((stage.root+"/"+name).c_str(),target.c_str())!=0) {
      error=httpAborted()?"Transfer stopped":"Cannot save font";return false;
    }
  }
  return fontPackInstalled(pack);
}
inline bool downloadFontPack(const FontPack& pack,std::string& error) {
  if(fontPackInstalled(pack))return true;
  const auto root=fontPackRoot();
  if(!fontPackDirectory(root)||!fontPackDirectory(root+"/ttf")||!fontPackDirectory(root+"/licenses")) {
    error="Cannot create font folder";return false;
  }
  std::string temp=root+"/.font-pack-XXXXXX";
  if(!::mkdtemp(temp.data())){error="Cannot save font";return false;}
  FontPackStage stage{temp,{"download.zip"}};
  if(!fontPackDirectory(temp+"/ttf")||!fontPackDirectory(temp+"/licenses")) {
    error="Cannot save font";return false;
  }
  const auto archive=temp+"/download.zip";
  FILE* f=std::fopen(archive.c_str(),"wb");
  if(!f){error="Cannot save font";return false;}
  FontPackSha256 sha;
  const std::string url=std::string("https://github.com/lampsonlnguyen/q2-books-fonts/releases/download/")+
                        fontPackRelease+"/"+pack.slug+".zip";
  const auto response=httpsRequest(url,[&](const char* bytes,size_t count) {
    if(httpAborted() || std::fwrite(bytes,1,count,f)!=count)return false;
    sha.update(bytes,count);return true;
  },pack.bytes,"GET","",{},0,true);
  bool ok=response.ok() && response.bytes==pack.bytes && sha.hexDigest()==pack.sha256;
  if(std::fflush(f)!=0)ok=false;
  if(ok && ::fsync(fileno(f))!=0)ok=false;
  if(std::fclose(f)!=0)ok=false;
  if(!ok){error=response.error.empty()?"Font checksum failed":response.error;return false;}
  if(!installVerifiedFontArchive(pack,stage,archive,error)) {
    if(error.empty())error="Font install incomplete";
    return false;
  }
  return true;
}
// Recover staging directories left by a forced shutdown. Only delete the
// exact files this installer can create; never recurse into arbitrary paths.
inline void sweepFontPackStages() {
  const auto root=fontPackRoot();
  DIR* d=::opendir(root.c_str());
  if(!d)return;
  while(dirent* ent=::readdir(d)) {
    const std::string name=ent->d_name;
    if(name.rfind(".font-pack-",0)!=0 || name.size()!=17)continue;
    const auto dir=root+"/"+name;
    struct stat st{};
    if(::lstat(dir.c_str(),&st)!=0 || !S_ISDIR(st.st_mode))continue;
    FontPackStage stage{dir,{"download.zip"}};
    // Do not follow symlinks in an abandoned staging directory.
    for(const char* sub:{"ttf","licenses"}) {
      if(::lstat((dir+"/"+sub).c_str(),&st)==0 && !S_ISDIR(st.st_mode)) {
        (void)::unlink((dir+"/"+sub).c_str());
      }
    }
    for(const auto& pack:fontPacks) {
      for(int pt=8;pt<=14;++pt)
        stage.files.push_back(std::string(pack.slug)+"-"+std::to_string(pt)+".cpfont");
      for(const char* style:{"regular","bold","italic","bolditalic"})
        stage.files.push_back(std::string("ttf/")+pack.slug+"-"+style+".ttf");
      stage.files.push_back(std::string("licenses/")+pack.slug+".txt");
    }
  }
  ::closedir(d);
}
} // namespace books
