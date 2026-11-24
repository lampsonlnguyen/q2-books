// SPDX-License-Identifier: MIT
#include "UserGuide.hpp"
#include "TxtPackage.hpp"

#include <cassert>
#include <fstream>
#include <iterator>

static std::string payload;
static int downloads=0;
static bool online=true;

static books::HttpResponse fetch(const books::HttpSink& sink) {
  ++downloads;
  if(!online)return {0,0,"Offline"};
  assert(sink(payload.data(),payload.size()));
  return {200,payload.size(),{}};
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
  payload=books::buildTxtEpub("Guide.txt","First edition");
  const auto path=books::openUserGuide(error,fetch);
  assert(!path.empty() && books::validUserGuideEpub(path) && read(path)==payload && error.empty());
  struct stat first{},second{};
  assert(stat(path.c_str(),&first)==0);
  assert(books::openUserGuide(error,fetch)==path && error.empty());
  assert(stat(path.c_str(),&second)==0 && first.st_ino==second.st_ino);

  payload=books::buildTxtEpub("Guide.txt","Newer edition");
  assert(books::openUserGuide(error,fetch)==path && read(path)==payload && error.empty());
  const auto latest=payload;
  payload="Not an EPUB";
  assert(books::openUserGuide(error,fetch)==path && read(path)==latest && !error.empty());
  online=false;
  assert(books::openUserGuide(error,fetch)==path && read(path)==latest && !error.empty());
  assert(downloads==6);
  assert(unlink(path.c_str())==0);
  assert(rmdir(dir)==0);
}
