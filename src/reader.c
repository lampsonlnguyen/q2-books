/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crosspod.h"
void reader_init(Reader *r) {
  r->book_len=0;r->line_count=0;r->page=0;r->wheel_acc=0;
  r->quit=0;r->loaded=0;r->touch_down=0;r->center_down=0;
  r->book[0]=0;
  r->warmth=35;r->night=0;r->tone_open=0;r->tone_dirty=0;
}
void reader_index(Reader *r) {
  unsigned pos=0,col=0;
  r->line_count=0;r->page=0;
  if(!r->book_len)return;
  r->starts[r->line_count++]=0;
  while(pos<r->book_len && r->line_count<LINE_MAX) {
    unsigned char ch=(unsigned char)r->book[pos++];
    if(ch=='\r')continue;
    if(ch=='\n'){col=0;if(pos<r->book_len)r->starts[r->line_count++]=pos;continue;}
    col++;
    if(col>=COLS && pos<r->book_len){
      r->starts[r->line_count++]=pos;col=0;
    }
  }
}
static unsigned pages(const Reader *r){return (r->line_count+PAGE_LINES-1)/PAGE_LINES;}
static void page_move(Reader *r,int change){
  unsigned n=pages(r);
  if(change>0 && r->page+1<n)r->page++;
  else if(change<0 && r->page>0)r->page--;
}
static int parse(const char *p){
  int sign=1,n=0;
  if(*p=='-'){sign=-1;p++;}
  while(*p>='0'&&*p<='9'){n=n*10+*p-'0';p++;if(n>100000)break;}
  return n*sign;
}
static int clamp_tone(int v){return v<0?0:v>100?100:v;}
static int tone_change(Reader *r,int change){
  int next=clamp_tone(r->warmth+change);
  if(next==r->warmth)return 0;
  r->warmth=next;r->tone_dirty=1;return 1;
}
int reader_input(Reader *r,const char *msg) {
  if(!msg || !msg[0])return 0;
  if(msg[0]=='w'&&msg[1]==' ') {
    int v=parse(msg+2);if(v>2000||v< -2000)return 0;
    if(r->tone_open) {
      int step=(-v)/18;
      if(step==0&&v!=0)step=v<0?1:-1;
      return tone_change(r,step);
    }
    int old=r->wheel_acc;
    int next=old-v;
    if((old<0&&-v>0)||(old>0&&-v<0))next=-v;
    r->wheel_acc=next;
    if(next>=35){page_move(r,1);r->wheel_acc=0;return 1;}
    if(next<=-35){page_move(r,-1);r->wheel_acc=0;return 1;}
    return 0;
  }
  if(msg[0]=='k'&&msg[1]==' '&&(msg[2]=='u'||msg[2]=='d')&&msg[3]==' ') {
    int key=parse(msg+4);
    if(key==218){
      if(msg[2]=='u'){r->center_down=0;return 0;}
      if(r->center_down)return 0;
      r->center_down=1;
      if(r->tone_open){r->night=!r->night;r->tone_dirty=1;return 1;}
      page_move(r,1);return 1;
    }
    if(msg[2]!='u')return 0;
    if(key==170){if(r->tone_open)r->tone_open=0;else r->quit=1;return 1;}
    if(key==172||key==222){if(r->tone_open)return tone_change(r,-5);page_move(r,-1);return 1;}
    if(key==173||key==223){if(r->tone_open)return tone_change(r,5);page_move(r,1);return 1;}
    return 0;
  }
  if(msg[0]=='t'&&msg[1]==' '&&(msg[2]=='d'||msg[2]=='u')&&msg[3]==' ') {
    if(msg[2]=='u'){r->touch_down=0;return 0;}
    if(r->touch_down)return 0;
    r->touch_down=1;
    int x=parse(msg+4);if(x<0||x>=SCREEN_W)return 0;
    const char *p=msg+4;
    while(*p&&*p!=' ')p++;
    int y=*p==' '?parse(p+1):-1;
    if(y<0||y>=SCREEN_H)return 0;
    if(r->tone_open){
      if(y>=141&&y<=209){int next=clamp_tone((x-46)*100/283);return tone_change(r,next-r->warmth);}
      if(y>=215&&y<=260){r->night=!r->night;r->tone_dirty=1;return 1;}
      if(y<52){r->tone_open=0;return 1;}
      return 0;
    }
    if(y<34&&x>=235){r->tone_open=1;return 1;}
    page_move(r,x<SCREEN_W/2?-1:1);return 1;
  }
  return 0;
}
