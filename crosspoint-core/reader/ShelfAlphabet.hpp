// SPDX-License-Identifier: MIT
#pragma once

#include "UnicodeCase.hpp"
#include <algorithm>
#include <array>
#include <string_view>

namespace books {
namespace alphabet {
// Latin-1, Extended-A/B and Extended Additional letters grouped by their
// ASCII base. Accents affect neither the jump group nor the primary sort key.
inline constexpr std::u32string_view latinAccents[]={
  U"ÀÁÂÃÄÅÆàáâãäåæĀāĂăĄąǍǎǞǟǠǡǺǻȀȁȂȃȦȧḀḁẠạẢảẤấẦầẨẩẪẫẬậẮắẰằẲẳẴẵẶặ",  // A
  U"ḂḃḄḅḆḇ",  // B
  U"ÇçĆćĈĉĊċČčḈḉ",  // C
  U"ÐðĎďĐđǄǅǆǱǲǳḊḋḌḍḎḏḐḑḒḓ",  // D
  U"ÈÉÊËèéêëĒēĔĕĖėĘęĚěȄȅȆȇȨȩḔḕḖḗḘḙḚḛḜḝẸẹẺẻẼẽẾếỀềỂểỄễỆệ",  // E
  U"Ḟḟ",  // F
  U"ĜĝĞğĠġĢģǦǧǴǵḠḡ",  // G
  U"ĤĥĦħȞȟḢḣḤḥḦḧḨḩḪḫẖ",  // H
  U"ÌÍÎÏìíîïĨĩĪīĬĭĮįİıĲĳǏǐȈȉȊȋḬḭḮḯỈỉỊị",  // I
  U"Ĵĵǰ",  // J
  U"ĶķǨǩḰḱḲḳḴḵ",  // K
  U"ĹĺĻļĽľŁłǇǈǉḶḷḸḹḺḻḼḽ",  // L
  U"ḾḿṀṁṂṃ",  // M
  U"ÑñŃńŅņŇňǊǋǌǸǹṄṅṆṇṈṉṊṋ",  // N
  U"ÒÓÔÕÖØòóôõöøŌōŎŏŐőŒœƠơǑǒǪǫǬǭȌȍȎȏȪȫȬȭȮȯȰȱṌṍṎṏṐṑṒṓỌọỎỏỐốỒồỔổỖỗỘộỚớỜờỞởỠỡỢợ",  // O
  U"ṔṕṖṗ",  // P
  U"",  // Q
  U"ŔŕŖŗŘřȐȑȒȓṘṙṚṛṜṝṞṟ",  // R
  U"ßŚśŜŝŞşŠšſȘșṠṡṢṣṤṥṦṧṨṩẛẞ",  // S
  U"ÞþŢţŤťȚțṪṫṬṭṮṯṰṱẗ",  // T
  U"ÙÚÛÜùúûüŨũŪūŬŭŮůŰűŲųƯưǓǔǕǖǗǘǙǚǛǜȔȕȖȗṲṳṴṵṶṷṸṹṺṻỤụỦủỨứỪừỬửỮữỰự",  // U
  U"ṼṽṾṿ",  // V
  U"ŴŵẀẁẂẃẄẅẆẇẈẉẘ",  // W
  U"ẊẋẌẍ",  // X
  U"ÝýÿŶŷŸȲȳẎẏẙỲỳỴỵỶỷỸỹ",  // Y
  U"ŹźŻżŽžẐẑẒẓẔẕ",  // Z
};
constexpr auto latinTable() {
  std::array<char,0x250-0xc0+0x100> table{};
  for(size_t i=0;i<std::size(latinAccents);++i)
    for(const auto cp:latinAccents[i])
      table[cp<0x250?cp-0xc0:0x250-0xc0+cp-0x1e00]=char('a'+i);
  return table;
}
inline constexpr auto latinBases=latinTable();
inline uint32_t fold(uint32_t cp) {
  if(cp>=0xff01 && cp<=0xff5e)cp-=0xfee0;  // fullwidth ASCII
  char latin=0;
  if(cp>=0xc0 && cp<0x250)latin=latinBases[cp-0xc0];
  if(cp>=0x1e00 && cp<0x1f00)latin=latinBases[0x250-0xc0+cp-0x1e00];
  return latin?static_cast<uint32_t>(latin):lowerCodepoint(cp);
}
inline bool asciiLetter(uint32_t cp) {return cp>='a' && cp<='z';}
inline bool mark(uint32_t cp) {
  return (cp>=0x300 && cp<=0x36f) || (cp>=0x1ab0 && cp<=0x1aff) ||
         (cp>=0x1dc0 && cp<=0x1dff) || (cp>=0x20d0 && cp<=0x20ff) ||
         (cp>=0xfe20 && cp<=0xfe2f);
}
inline bool space(uint32_t cp) {
  return cp<=0x20 || cp==0x85 || cp==0xa0 || cp==0x1680 ||
      (cp>=0x2000 && cp<=0x200a) || cp==0x2028 || cp==0x2029 ||
      cp==0x202f || cp==0x205f || cp==0x3000;
}
struct Script {uint32_t first,last;size_t group;};
inline constexpr const char* scripts[]={
  "Greek","Cyrillic","Hebrew","Arabic","Indic","Thai","Kana","Han","Hangul","Other"
};
inline constexpr Script scriptRanges[]={
  {0x370,0x3ff,0},{0x1f00,0x1fff,0},{0x400,0x52f,1},
  {0x590,0x5ff,2},{0xfb1d,0xfb4f,2},{0x600,0x6ff,3},{0x750,0x77f,3},
  {0x8a0,0x8ff,3},{0xfb50,0xfdff,3},{0xfe70,0xfeff,3},
  {0x900,0xdff,4},{0xe00,0xe7f,5},{0x3040,0x30ff,6},{0x31f0,0x31ff,6},
  {0xff66,0xff9f,6},{0x3400,0x9fff,7},{0xf900,0xfaff,7},{0x20000,0x323af,7},
  {0x1100,0x11ff,8},{0x3130,0x318f,8},{0xac00,0xd7ff,8},
};
inline constexpr size_t scriptStart=28;
inline constexpr size_t groupCount=scriptStart+std::size(scripts);
inline size_t group(uint32_t cp) {
  if(asciiLetter(cp))return 2+cp-'a';
  constexpr uint32_t digitStarts[]={0x30,0x660,0x6f0,0x966,0x9e6,0xa66,0xae6,
    0xb66,0xbe6,0xc66,0xce6,0xd66,0xde6,0xe50,0xed0,0xf20};
  for(const auto first:digitStarts)if(cp>=first && cp<first+10)return 1;
  for(const auto& script:scriptRanges)
    if(cp>=script.first && cp<=script.last)return scriptStart+script.group;
  return cp<0x300 || cp>=0x80000000u || (cp>=0x2000 && cp<=0x303f) ||
      (cp>=0x1f000 && cp<=0x1faff)?0:groupCount-1;
}
inline size_t titleGroup(const std::string& title) {
  for(size_t i=0;i<title.size();) {
    const auto cp=utf8case::next(title,i);
    if(!space(cp))return group(fold(cp));
  }
  return 0;
}
inline std::string label(size_t groupId) {
  if(groupId==0)return "#";
  if(groupId==1)return "0-9";
  if(groupId<scriptStart)return std::string(1,char('A'+groupId-2));
  return scripts[groupId-scriptStart];
}
inline std::string sortKey(const std::string& title) {
  std::string key;
  key.reserve(title.size()+1);
  key+=char(titleGroup(title)+1);
  bool latin=false;
  for(size_t i=0;i<title.size();) {
    const auto cp=fold(utf8case::next(title,i));
    if(key.size()==1 && space(cp))continue;
    if(latin && mark(cp))continue;
    utf8case::put(key,cp);
    latin=asciiLetter(cp);
  }
  return key;
}
} // namespace alphabet
} // namespace books
