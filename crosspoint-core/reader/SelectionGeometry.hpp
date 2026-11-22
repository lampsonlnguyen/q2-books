// SPDX-License-Identifier: MIT
#pragma once
// Reuse original CrossPoint word extents and logical text offsets. Never
// reconstruct selected text from the framebuffer (loses Unicode shaping).
#include <Epub/Page.h>
#include <GfxRenderer.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

namespace crosspod {
struct SelectedWord {
  uint32_t begin=0,end=0;
  int x=0,y=0,width=0,height=0;
  std::string text;
};
inline std::vector<SelectedWord> selectableWords(
    const Page& page,const GfxRenderer& renderer,int bookFontId,
    int margin,int top) {
  std::vector<SelectedWord> result;
  result.reserve(72);
  for(const auto& element:page.elements) {
    if(element->getTag()!=TAG_PageLine)continue;
    const auto* block=static_cast<const PageLine*>(element.get())->getBlock();
    if(!block)continue;
    const int font=TextBlock::renderFontId(
        renderer,block->blockFontId(bookFontId),bookFontId);
    const int ascent=std::max(12,renderer.getFontAscenderSize(font));
    for(uint16_t i=0;i<block->wordCount() && result.size()<512;++i) {
      const auto range=block->wordSourceRange(i);
      if(range.start==UINT32_MAX || range.end<=range.start)continue;
      const int wordFont=TextBlock::renderFontId(
          renderer,block->wordFontId(i,bookFontId),bookFontId);
      const int width=renderer.getTextAdvanceX(
          wordFont,block->laidOutWord(i),block->wordStyle(i));
      if(width<=0)continue;
      const int x=margin+element->xPos+block->wordXpos(i);
      // PageLine::yPos is the top of the line: GfxRenderer::drawText puts
      // the baseline at y + ascender. (Treating it as the baseline put every
      // touch box one line too high, so a press picked the word below.)
      // Include diacritics and ruby above it, and a small touch slop below.
      const int y=top+element->yPos-7;
      result.push_back({range.start,range.end,x,y,width,ascent+15,
                        block->wordText(i)});
    }
  }
  return result;
}
inline int hitSelectableWord(const std::vector<SelectedWord>& words,int x,int y) {
  int best=-1;
  int distance=2147483647;
  for(size_t i=0;i<words.size();++i) {
    const auto& w=words[i];
    const int dx=x<w.x?w.x-x:(x>=w.x+w.width?x-(w.x+w.width):0);
    const int dy=y<w.y?w.y-y:(y>=w.y+w.height?y-(w.y+w.height):0);
    if(dx>12||dy>8)continue;
    const int d=dx*dx+dy*dy;
    if(d<distance){distance=d;best=static_cast<int>(i);}
  }
  return best;
}
inline bool overlapsVisibleWord(const SelectedWord& word,
                               uint32_t begin,uint32_t end) {
  return word.begin<end && begin<word.end;
}
} // namespace crosspod
