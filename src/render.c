/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crosspod.h"
#include "font.h"
static void rect(u32 *d,unsigned s,int x,int y,int w,int h,u32 color) {
  int x0=x<0?0:x,y0=y<0?0:y,x1=x+w>SCREEN_W?SCREEN_W:x+w,y1=y+h>SCREEN_H?SCREEN_H:y+h;
  if(x1<=x0||y1<=y0)return;
  for(int px=x0;px<x1;px++)for(int py=y0;py<y1;py++)d[px*s+SCREEN_H-1-py]=0xff000000u|color;
}
static int text(u32 *d,unsigned s,int x,int y,const char *str,int count,u32 color) {
  for(int i=0;i<count&&str[i];i++) {
    unsigned ch=(unsigned char)str[i];
    if(ch=='\n')break;
    if(ch=='\r')continue;
    if(ch>='a'&&ch<='z')ch-=32;
    if(ch<32||ch>90)ch='?';
    unsigned index=ch-32, width=f12_adv[index];
    for(int py=0;py<12;py++)for(unsigned px=0;px<width;px++){
      unsigned alpha=f12_px[f12_off[index]+py*width+px];
      int dx=x+(int)px,dy=y+py;
      if(!alpha||dx<0||dx>=SCREEN_W||dy<0||dy>=SCREEN_H)continue;
      u32 *out=&d[dx*s+SCREEN_H-1-dy],old=*out;
      unsigned red=(((old>>16)&255u)*(255u-alpha)+((color>>16)&255u)*alpha)/255u;
      unsigned green=(((old>>8)&255u)*(255u-alpha)+((color>>8)&255u)*alpha)/255u;
      unsigned blue=((old&255u)*(255u-alpha)+(color&255u)*alpha)/255u;
      *out=0xff000000u|(red<<16)|(green<<8)|blue;
    }
    x+=(int)width;
    if(x>SCREEN_W-12)break;
  }
  return x;
}
static void number(u32 *d,unsigned s,int x,int y,unsigned n,u32 color){
  char digits[16];int pos=15;digits[pos]=0;
  do { digits[--pos]=(char)('0'+n%10u);n/=10u; }while(n&&pos);
  text(d,s,x,y,digits,15-pos,color);
}
static u32 blend(u32 cool,u32 warm,int amount){
  unsigned a=(unsigned)(amount<0?0:amount>100?100:amount);
  unsigned ia=100-a;
  unsigned red=(((cool>>16)&255u)*ia+((warm>>16)&255u)*a)/100;
  unsigned green=(((cool>>8)&255u)*ia+((warm>>8)&255u)*a)/100;
  unsigned blue=((cool&255u)*ia+(warm&255u)*a)/100;
  return red<<16|green<<8|blue;
}
void reader_render(const Reader *r,u32 *d,unsigned s){
  u32 paper=blend(0xe5f2ff,0xffd7a4,r->warmth);
  u32 ink=blend(0x203547,0x483526,r->warmth);
  u32 muted=blend(0x587381,0x896748,r->warmth);
  u32 accent=blend(0x377693,0xa86b34,r->warmth);
  if(r->night){
    paper=blend(0x101c28,0x261b16,r->warmth);
    ink=blend(0xb7cfe1,0xffd5a6,r->warmth);
    muted=blend(0x8cabc4,0xc09a71,r->warmth);
    accent=blend(0x628eac,0xbd8b56,r->warmth);
  }
  rect(d,s,0,0,SCREEN_W,SCREEN_H,paper);
  rect(d,s,0,0,SCREEN_W,32,ink);
  text(d,s,15,10,"CROSSPOD",8,0xffffff);
  text(d,s,256,10,"TONE",4,0xffffff);
  text(d,s,307,10,r->night?"NIGHT":"WARM",r->night?5:4,0xffffff);
  if(!r->loaded){
    text(d,s,20,70,"BOOK NOT FOUND",14,ink);
    text(d,s,20,101,"/.CROSSPOD/BOOK.TXT",20,muted);
    text(d,s,20,135,"ADD A TXT FILE TO SD CARD",25,muted);
  } else if(!r->line_count){
    text(d,s,20,75,"BOOK IS EMPTY",13,ink);
  }else{
    unsigned begin=r->page*PAGE_LINES;
    for(unsigned row=0;row<PAGE_LINES && begin+row<r->line_count;row++){
      unsigned from=r->starts[begin+row];
      unsigned to=(begin+row+1<r->line_count)?r->starts[begin+row+1]:r->book_len;
      if(to>from+COLS)to=from+COLS;
      text(d,s,17,41+(int)row*16,r->book+from,(int)(to-from),ink);
    }
  }
  rect(d,s,0,302,SCREEN_W,18,ink);
  text(d,s,14,305,"WHEEL: PAGE   BACK: EXIT",24,0xffffff);
  if(r->loaded&&r->line_count){
    number(d,s,294,305,r->page+1,0xffffff);
    text(d,s,320,305,"/",1,0xffffff);
    number(d,s,334,305,(r->line_count+PAGE_LINES-1)/PAGE_LINES,0xffffff);
    rect(d,s,0,297,SCREEN_W,3,0xd1d6cf);
    rect(d,s,0,297,(int)((r->page+1)*SCREEN_W/((r->line_count+PAGE_LINES-1)/PAGE_LINES)),3,accent);
  }
  if(r->tone_open){
    const u32 panel=r->night?0x2d2927:0xf4f1e9;
    const u32 panel_ink=r->night?0xf0d7b8:0x272e35;
    rect(d,s,18,58,339,222,r->night?0x0d1116:0x73858b);
    rect(d,s,22,62,331,214,panel);
    text(d,s,40,79,"READING TONE",12,panel_ink);
    text(d,s,40,114,"COOL",4,panel_ink);
    text(d,s,292,114,"WARM",4,panel_ink);
    for(int x=46;x<=329;x++){
      u32 sample=blend(0xbadfff,0xffba71,(x-46)*100/283);
      rect(d,s,x,145,1,32,sample);
    }
    int marker=46+r->warmth*283/100;
    rect(d,s,marker-3,139,7,45,r->night?0xffffff:0x202833);
    text(d,s,48,189,"WHEEL TO ADJUST",15,panel_ink);
    rect(d,s,45,221,286,37,r->night?0x544030:0xe0e5e5);
    text(d,s,65,233,r->night?"NIGHT: ON":"NIGHT: OFF",r->night?9:10,panel_ink);
    text(d,s,38,263,"BACK TO CLOSE",13,panel_ink);
  }
}
