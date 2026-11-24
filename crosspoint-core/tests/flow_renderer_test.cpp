// SPDX-License-Identifier: MIT
#include "FlowRenderer.hpp"
#include "ShelfProgress.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <vector>
#include <unistd.h>
int main() {
  using namespace books;
  std::vector<uint8_t> rgb(300*450*3,0);
  for(size_t i=0;i<rgb.size();i+=3)rgb[i]=255;
  FlowCover c;
  assert(makeFlowCover(rgb.data(),rgb.size(),300,450,c));
  assert(c.width==133 && c.height==200);
  assert(c.pixels[0]==0xf800);
  assert(!makeFlowCover(rgb.data(),1,300,450,c));
  assert(flow::hit(c,0,187,120));
  assert(!flow::hit(c,0,0,120));
  assert(!flow::hit(c,0,187,300));
  std::vector<uint32_t> frame(flow::width*flow::height+2,0xffeeeeee);
  for(float d=-4;d<=4;d+=0.125f) {
    // Each animation position starts with a fresh background.
    std::fill(frame.begin()+1,frame.end()-1,0xffeeeeee);
    flow::draw(frame.data()+1,c,d,0xeeeeee);
    assert(frame.front()==0xffeeeeee && frame.back()==0xffeeeeee);
  }
  std::fill(frame.begin()+1,frame.end()-1,0xffeeeeee);
  flow::draw(frame.data()+1,c,0.f,0xeeeeee);
  assert(frame[1+187+120*flow::width]==0xffff0000);
  assert(frame[1+187+10*flow::width]==0xffeeeeee);
  char dir[]="/tmp/books-flow-XXXXXX";
  assert(mkdtemp(dir));
  assert(setenv("BOOKS_STATE_DIR",dir,1)==0);
  const std::string book=std::string(dir)+"/book.epub";
  FILE* f=std::fopen(book.c_str(),"wb");
  assert(f);assert(std::fputs("book",f)>=0);assert(std::fclose(f)==0);
  assert(saveFlowCover(book,c));
  FlowCover loaded;
  assert(loadFlowCover(book,loaded)&&loaded.pixels==c.pixels);
  uint32_t progress=0;
  assert(!loadShelfProgress(book,progress));
  assert(saveShelfProgress(book,420000));
  assert(loadShelfProgress(book,progress)&&progress==420000);
  f=std::fopen(book.c_str(),"ab");assert(f);assert(std::fputc('x',f)!=EOF);assert(std::fclose(f)==0);
  assert(!loadFlowCover(book,loaded));
  assert(!loadShelfProgress(book,progress));
  unlink(flowCoverFile(book).c_str());
  unlink((positionFile(book)+".percent").c_str());
  unlink(book.c_str());rmdir(dir);
}
