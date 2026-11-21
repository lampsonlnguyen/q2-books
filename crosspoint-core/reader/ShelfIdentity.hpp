// SPDX-License-Identifier: MIT
#pragma once
// Parse two unsigned file-identity fields without glibc's C23 scanf/strto*
// entry points: Q2's stock MIPS glibc predates __isoc23_* symbols.
#include <climits>
#include <cstring>

namespace crosspod {
inline bool parseIdentityNumber(const char*& p,unsigned long long& value) {
  if(*p<'0'||*p>'9')return false;
  unsigned long long parsed=0;
  do {
    const unsigned digit=static_cast<unsigned>(*p-'0');
    if(parsed>(ULLONG_MAX-digit)/10)return false;
    parsed=parsed*10+digit;
    ++p;
  } while(*p>='0'&&*p<='9');
  value=parsed;
  return true;
}
inline bool parseShelfIdentityHeader(const char* line,const char* magic,
                                    unsigned long long& size,
                                    unsigned long long& modified) {
  const size_t len=std::strlen(magic);
  if(std::strncmp(line,magic,len)!=0 || line[len]!=' ')return false;
  const char* p=line+len+1;
  if(!parseIdentityNumber(p,size)||*p!=' ')return false;
  ++p;
  if(!parseIdentityNumber(p,modified))return false;
  return *p=='\n'||*p=='\0';
}
} // namespace crosspod
