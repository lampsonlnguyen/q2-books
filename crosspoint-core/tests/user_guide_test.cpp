// SPDX-License-Identifier: MIT
#include "UserGuide.hpp"
#include "TxtPackage.hpp"

#include <cassert>
#include <fstream>
#include <iterator>

static std::string payload;
static std::string remoteEtag;
static int requests=0,downloads=0;
static bool online=true;

static books::HttpResponse fetch(const books::HttpSink& sink,const std::string& etag) {
  ++requests;
  if(!online)return {0,0,"Offline"};
  if(!etag.empty() && etag==remoteEtag)return {304,0,{},remoteEtag};
  ++downloads;
  assert(sink(payload.data(),payload.size()));
  return {200,payload.size(),{},remoteEtag};
}

static std::string read(const std::string& path) {
  std::ifstream file(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(file),{}};
}

int main() {
  const std::string url=std::string("https://github.com")+books::q2PodGuideAssetPath;
  const std::string cdn="https://release-assets.githubusercontent.com/asset?sig=abc";
  assert(books::githubReleaseAssetRedirect(url,cdn)==cdn);
  assert(books::githubReleaseAssetRedirect(url,"https://example.org/asset").empty());
  assert(books::githubReleaseAssetRedirect(
      "https://github.com/DiamondBond/q2-pod/releases/download/guide/other.epub",cdn).empty());

  char dir[]="/tmp/books-guide-test-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  std::string error;
  online=false;
  assert(books::openUserGuide(error,fetch).empty() && !error.empty());

  online=true;
  remoteEtag="\"guide-1\"";
  payload=books::buildTxtEpub("Guide.txt","First edition");
  const auto path=books::openUserGuide(error,fetch);
  assert(!path.empty() && books::validUserGuideEpub(path) && read(path)==payload && error.empty());
  struct stat first{},second{};
  assert(stat(path.c_str(),&first)==0);
  assert(books::openUserGuide(error,fetch)==path && error.empty());
  assert(stat(path.c_str(),&second)==0 && first.st_ino==second.st_ino && downloads==1);
  // A v1.3.3 cache has no validator. Bootstrap it once without replacing
  // identical EPUB bytes, then check even a same-size update by its tag.
  assert(unlink((path+".etag").c_str())==0);
  assert(books::openUserGuide(error,fetch)==path && downloads==2 && error.empty());
  assert(stat(path.c_str(),&second)==0 && first.st_ino==second.st_ino);

  remoteEtag="\"guide-2\"";
  payload=books::buildTxtEpub("Guide.txt","Other edition");
  assert(static_cast<off_t>(payload.size())==first.st_size);
  assert(books::openUserGuide(error,fetch)==path && read(path)==payload && error.empty());
  const auto latest=payload;
  remoteEtag="\"bad\"";
  payload="Not an EPUB";
  assert(books::openUserGuide(error,fetch)==path && read(path)==latest && !error.empty());
  online=false;
  assert(books::openUserGuide(error,fetch)==path && read(path)==latest && !error.empty());
  assert(requests==7 && downloads==4);
  assert(books::cachedUserGuideEtag(path)=="\"guide-2\"");
  assert(unlink((path+".etag").c_str())==0);
  assert(unlink(path.c_str())==0);
  assert(rmdir(dir)==0);
}
