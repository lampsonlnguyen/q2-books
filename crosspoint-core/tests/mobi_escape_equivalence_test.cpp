#include "TxtPackage.hpp"
#include <chrono>
#include <cstdio>
#include <cstdlib>

static std::string reference(std::string_view s) {
  std::string out;
  TxtToHtml::appendEscapedHtml(out,s);
  std::string quoted;
  for(char c:out)quoted+=c=='"'?std::string("&quot;"):std::string(1,c);
  return quoted;
}
int main() {
  std::string bytes;
  for(unsigned i=0;i<256;++i)bytes+=static_cast<char>(i);
  std::string large;
  const std::string paragraph="Đà Lạt &amp; Nguyễn <sách> \"Vietnamese\" ";
  for(unsigned i=0;i<50000;++i)large+=paragraph;
  for(const auto& s:{std::string{},bytes,paragraph,large}) {
    if(reference(s)!=books::txtpkg::xmlEscape(s))return EXIT_FAILURE;
  }
  using Clock=std::chrono::steady_clock;
  const auto start=Clock::now();
  const auto old=reference(large);
  const auto mid=Clock::now();
  const auto optimized=books::txtpkg::xmlEscape(large);
  const auto end=Clock::now();
  if(old!=optimized)return EXIT_FAILURE;
  std::printf("XML escape bytes=%zu baseline_us=%lld optimized_us=%lld\n",large.size(),
      static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(mid-start).count()),
      static_cast<long long>(std::chrono::duration_cast<std::chrono::microseconds>(end-mid).count()));
}
