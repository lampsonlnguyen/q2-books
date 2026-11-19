/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crosspod.h"
static Reader *unused_pointer;
void reader_init(Reader *r) {
  r->book_len=0;r->line_count=0;r->page=0;r->wheel_acc=0;
  r->quit=0;r->loaded=0;r->touch_down=0;r->center_down=0;
  r->book[0]=0;unused_pointer=r;
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
int reader_input(Reader *r,const char *msg) {
  if(!msg || !msg[0])return 0;
  if(msg[0]=='w'&&msg[1]==' ') {
    int v=parse(msg+2);if(v>2000||v< -2000)return 0;
    int old=r->wheel_acc;
    int next=old-v;
    if((old<0&&-v>0)||(old>0&&-v<0))next=-v;
    r->wheel_acc=(unsigned)next;
    if(next>=35){page_move(r,1);r->wheel_acc=0;return 1;}
    if(next<=-35){page_move(r,-1);r->wheel_acc=0;return 1;}
    return 0;
  }
  if(msg[0]=='k'&&msg[1]==' '&&(msg[2]=='u'||msg[2]=='d')&&msg[3]==' ') {
    int key=parse(msg+4);
    if(key==218){
      if(msg[2]=='u'){r->center_down=0;return 0;}
      if(r->center_down)return 0;
      r->center_down=1;page_move(r,1);return 1;
    }
    if(msg[2]!='u')return 0;
    if(key==170){r->quit=1;return 1;}
    if(key==172||key==222){page_move(r,-1);return 1;}
    if(key==173||key==223){page_move(r,1);return 1;}
    return 0;
  }
  if(msg[0]=='t'&&msg[1]==' '&&(msg[2]=='d'||msg[2]=='u')&&msg[3]==' ') {
    if(msg[2]=='u'){r->touch_down=0;return 0;}
    if(r->touch_down)return 0;
    r->touch_down=1;
    int x=parse(msg+4);if(x<0||x>=SCREEN_W)return 0;
    page_move(r,x<SCREEN_W/2?-1:1);return 1;
  }
  return 0;
}
