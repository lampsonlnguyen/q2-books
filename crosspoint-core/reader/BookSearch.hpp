// SPDX-License-Identifier: MIT
#pragma once

#include "UnicodeCase.hpp"
#include <Utf8.h>
#include <VisibleTextUtils.h>
#include <deque>
#include <string_view>
#include <vector>
#include <expat.h>

namespace books {
inline bool bookSearchSpace(uint32_t cp) {
  return cp==' ' || (cp>='\t' && cp<='\r') || cp==0xa0 || cp==0x1680 ||
      (cp>=0x2000 && cp<=0x200a) || cp==0x2028 || cp==0x2029 || cp==0x202f ||
      cp==0x205f || cp==0x3000;
}

inline std::vector<uint32_t> bookSearchQuery(const std::string& text) {
  std::vector<uint32_t> query;
  if(text.size()>384)return query;
  const auto normalized=utf8ComposeNfc(text);
  for(size_t pos=0;pos<normalized.size();) {
    auto cp=utf8case::next(normalized,pos);
    if(cp==0xad)continue;
    if(bookSearchSpace(cp)) {
      if(query.empty() || query.back()==' ')continue;
      cp=' ';
    }
    query.push_back(lowerCodepoint(cp));
  }
  if(!query.empty() && query.back()==' ')query.pop_back();
  return query;
}

struct BookSearchMatch {
  int spine;
  uint32_t begin,end;
  std::string snippet;
};

class BookSearchChapter {
  struct Token {uint32_t cp,begin,end;};
  const std::vector<uint32_t>& query_;
  const int spine_;
  std::vector<BookSearchMatch>& results_;
  const size_t limit_;
  const uint32_t firstOffset_;
  const std::vector<size_t>& prefix_;
  std::deque<Token> tokens_;
  std::string cluster_;
  uint32_t offset_=0,clusterBegin_=0,clusterEnd_=0;
  size_t depth_=0,body_=0,nonVisible_=0,hidden_=0,matched_=0;

  void emit(Token token) {
    if(query_.empty() || results_.size()>=limit_)return;
    if(bookSearchSpace(token.cp)) {
      if(tokens_.empty() || tokens_.back().cp==' ')return;
      token.cp=' ';
    }
    tokens_.push_back(token);
    if(tokens_.size()>query_.size()+28)tokens_.pop_front();
    const auto cp=lowerCodepoint(token.cp);
    while(matched_ && query_[matched_]!=cp)matched_=prefix_[matched_-1];
    if(query_[matched_]!=cp)return;
    if(++matched_!=query_.size())return;

    matched_=prefix_.back();
    const auto begin=tokens_[tokens_.size()-query_.size()].begin;
    if(begin<firstOffset_)return;
    std::string snippet;
    if(tokens_.size()<token.end)snippet="…";
    for(const auto& part:tokens_)utf8case::put(snippet,part.cp);
    results_.push_back({spine_,begin,token.end,std::move(snippet)});
  }

  void flush() {
    if(cluster_.empty())return;
    const auto normalized=utf8ComposeNfc(cluster_);
    for(size_t pos=0;pos<normalized.size();) {
      const auto cp=utf8case::next(normalized,pos);
      if(cp!=0xad)emit({cp,clusterBegin_,clusterEnd_});
    }
    cluster_.clear();
  }

  static bool block(std::string_view tag) {
    for(const auto* name:{"p","div","br","li","dt","dd","tr","td","section","article",
                          "blockquote","pre","h1","h2","h3","h4","h5","h6"})
      if(VisibleTextUtils::equalsTag(tag,name))return true;
    return false;
  }

  static void XMLCALL start(void* user,const XML_Char* name,const XML_Char** atts) {
    auto& self=*static_cast<BookSearchChapter*>(user);
    ++self.depth_;
    if(VisibleTextUtils::equalsTag(name,"body"))self.body_=self.depth_;
    if(!self.body_)return;
    if(!self.nonVisible_ && VisibleTextUtils::isNonVisibleElement(name))self.nonVisible_=self.depth_;
    if(!self.hidden_ && VisibleTextUtils::equalsTag(name,"rt"))self.hidden_=self.depth_;
    for(size_t i=0;!self.hidden_ && atts[i];i+=2)
      if(std::string_view(atts[i])=="hidden")self.hidden_=self.depth_;
    if(!self.nonVisible_ && !self.hidden_ && block(name)) {
      self.flush();
      self.emit({' ',self.offset_,self.offset_});
    }
  }

  static void XMLCALL end(void* user,const XML_Char* name) {
    auto& self=*static_cast<BookSearchChapter*>(user);
    if(self.body_ && !self.nonVisible_ && !self.hidden_ && block(name)) {
      self.flush();
      self.emit({' ',self.offset_,self.offset_});
    }
    if(self.depth_==self.hidden_)self.hidden_=0;
    if(self.depth_==self.nonVisible_)self.nonVisible_=0;
    if(self.depth_==self.body_) {self.flush();self.body_=0;}
    --self.depth_;
  }

  static void XMLCALL data(void* user,const XML_Char* bytes,int size) {
    auto& self=*static_cast<BookSearchChapter*>(user);
    if(!self.body_ || self.nonVisible_)return;
    const auto* ptr=reinterpret_cast<const unsigned char*>(bytes);
    const auto* end=ptr+size;
    for(;ptr<end;) {
      const uint32_t cp=utf8NextCodepoint(&ptr);
      const uint32_t position=self.offset_++;
      if(self.hidden_ || cp==0xad || self.results_.size()>=self.limit_)continue;
      if(!utf8IsCombiningMark(cp) || self.cluster_.size()>=48)self.flush();
      if(self.cluster_.empty())self.clusterBegin_=position;
      utf8case::put(self.cluster_,cp);
      self.clusterEnd_=position+1;
    }
  }

 public:
  BookSearchChapter(const std::vector<uint32_t>& query,const std::vector<size_t>& prefix,
                    int spine,std::vector<BookSearchMatch>& results,size_t limit,uint32_t firstOffset=0)
      :query_(query),spine_(spine),results_(results),limit_(limit),firstOffset_(firstOffset),prefix_(prefix) {}

  void attach(XML_Parser parser) {
    XML_SetUserData(parser,this);
    XML_SetElementHandler(parser,start,end);
    XML_SetCharacterDataHandler(parser,data);
  }

  static std::vector<size_t> prefixTable(const std::vector<uint32_t>& query) {
    std::vector<size_t> prefix(query.size());
    size_t matched=0;
    for(size_t i=1;i<query.size();++i) {
      while(matched && query[matched]!=query[i])matched=prefix[matched-1];
      if(query[matched]==query[i])++matched;
      prefix[i]=matched;
    }
    return prefix;
  }
};
} // namespace books
