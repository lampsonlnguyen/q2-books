/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CROSSPOD_UTF8_H
#define CROSSPOD_UTF8_H
/* Strict, bounded UTF-8 decoder. Keeps page offsets in bytes. */
static unsigned cp_utf8_next(const char *p,unsigned remain,unsigned *used){
  if(!remain){*used=0;return 0;}
  const unsigned char *s=(const unsigned char *)p;
  unsigned a=s[0],need=0,cp=0,min=0;
  if(a<0x80){*used=1;return a;}
  if(a>=0xc2&&a<=0xdf){need=2;cp=a&31;min=0x80;}
  else if(a>=0xe0&&a<=0xef){need=3;cp=a&15;min=0x800;}
  else if(a>=0xf0&&a<=0xf4){need=4;cp=a&7;min=0x10000;}
  else {*used=1;return 0xfffd;}
  if(need>remain){*used=1;return 0xfffd;}
  for(unsigned i=1;i<need;i++){
    if((s[i]&0xc0)!=0x80){*used=1;return 0xfffd;}
    cp=(cp<<6)|(s[i]&63);
  }
  *used=need;
  if(cp<min||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return 0xfffd;
  return cp;
}
#endif
