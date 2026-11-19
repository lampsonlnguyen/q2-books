/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "crosspod.h"

static int ascii_lower(int c){return c>='A'&&c<='Z'?c+32:c;}
static int compare(const char *a,const char *b){
  while(*a&&*b){
    int d=ascii_lower((unsigned char)*a)-ascii_lower((unsigned char)*b);
    if(d)return d;
    a++;b++;
  }
  return (unsigned char)*a-(unsigned char)*b;
}
static int valid_relative(const char *p){
  unsigned n=0,segment=0;
  if(!p||!*p||*p=='/')return 0;
  for(;p[n];n++){
    if(n>=PATH_MAX-1)return 0;
    if(p[n]=='\\'||(unsigned char)p[n]<32)return 0;
    if(p[n]=='/'){
      if(n==segment)return 0;
      if(p[segment]=='.'&&(n==segment+1||(n==segment+2&&p[segment+1]=='.')))return 0;
      segment=n+1;
    }
  }
  if(segment==n)return 0;
  if(p[segment]=='.'&&(n==segment+1||(n==segment+2&&p[segment+1]=='.')))return 0;
  return 1;
}
static int supported(const char *p) {
  unsigned n=0;while(p[n])n++;
  if(n>=5&&p[n-4]=='.'&&ascii_lower(p[n-3])=='t'&&ascii_lower(p[n-2])=='x'&&ascii_lower(p[n-1])=='t')return 1;
  if(n>=6&&p[n-5]=='.'&&ascii_lower(p[n-4])=='e'&&ascii_lower(p[n-3])=='p'&&ascii_lower(p[n-2])=='u'&&ascii_lower(p[n-1])=='b')return 1;
  return 0;
}
int reader_catalog_add(Reader *r,const char *relative){
  if(!valid_relative(relative)||!supported(relative)||r->book_count>=LIB_MAX)return 0;
  for(unsigned i=0;i<r->book_count;i++)if(!compare(relative,r->paths[i]))return 0;
  unsigned i=r->book_count++,k=0;
  while(relative[k]&&k<PATH_MAX-1){r->paths[i][k]=relative[k];k++;}
  r->paths[i][k]=0;
  return 1;
}
void reader_catalog_sort(Reader *r){
  /* Insertion sort in place, once at startup: no memory allocation. */
  for(unsigned i=1;i<r->book_count;i++){
    for(unsigned j=i;j>0&&compare(r->paths[j-1],r->paths[j])>0;j--){
      for(unsigned k=0;k<PATH_MAX;k++){
        char t=r->paths[j-1][k];r->paths[j-1][k]=r->paths[j][k];r->paths[j][k]=t;
      }
    }
  }
}
void reader_init(Reader *r) {
  r->book_len=0;r->line_count=0;r->page=0;r->wheel_acc=0;
  r->quit=0;r->loaded=0;r->touch_down=0;r->center_down=0;
  r->warmth=35;r->night=0;r->tone_open=0;r->tone_dirty=0;
  r->book_count=0;r->selected=0;r->reading=0;r->open_request=0;r->book_error=0;
  r->book[0]=0;
}
void reader_open_result(Reader *r,int ok){
  r->open_request=0;
  r->loaded=ok!=0;
  r->reading=ok!=0;
  r->book_error=ok?0:1;
  r->tone_open=0;
  r->wheel_acc=0;
  if(!ok){r->page=0;r->line_count=0;r->book_len=0;r->book[0]=0;}
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
static void move(Reader *r,int dir){
  if(!r->reading){
    if(dir>0 && r->selected+1<r->book_count)r->selected++;
    else if(dir<0 && r->selected>0)r->selected--;
    return;
  }
  unsigned n=pages(r);
  if(dir>0 && r->page+1<n)r->page++;
  else if(dir<0 && r->page>0)r->page--;
}
static void open_selected(Reader *r){
  if(!r->reading && r->selected<r->book_count){
    r->open_request=(int)r->selected+1;
    r->book_error=0;
  }
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
  if(!msg||!msg[0])return 0;
  if(msg[0]=='w'&&msg[1]==' ') {
    int v=parse(msg+2);if(v>2000||v< -2000)return 0;
    if(r->tone_open){
      int step=(-v)/18;
      if(step==0&&v!=0)step=v<0?1:-1;
      return tone_change(r,step);
    }
    int old=r->wheel_acc,delta=-v;
    if((old<0&&delta>0)||(old>0&&delta<0))old=0;
    int next=old+delta;
    r->wheel_acc=next;
    int changed=0;
    while(r->wheel_acc>=35){move(r,1);r->wheel_acc-=35;changed=1;}
    while(r->wheel_acc<=-35){move(r,-1);r->wheel_acc+=35;changed=1;}
    return changed;
  }
  if(msg[0]=='k'&&msg[1]==' '&&(msg[2]=='u'||msg[2]=='d')&&msg[3]==' '){
    int key=parse(msg+4);
    if(key==218){
      if(msg[2]=='u'){r->center_down=0;return 0;}
      if(r->center_down)return 0;
      r->center_down=1;
      if(r->tone_open){r->night=!r->night;r->tone_dirty=1;}
      else if(!r->reading)open_selected(r);
      else move(r,1);
      return 1;
    }
    if(msg[2]!='u')return 0;
    if(key==170){
      if(r->tone_open)r->tone_open=0;
      else if(r->reading){r->reading=0;r->loaded=0;}
      else r->quit=1;
      return 1;
    }
    if(key==172||key==222){if(r->tone_open)return tone_change(r,-5);move(r,-1);return 1;}
    if(key==173||key==223){if(r->tone_open)return tone_change(r,5);move(r,1);return 1;}
    return 0;
  }
  if(msg[0]=='t'&&msg[1]==' '&&(msg[2]=='d'||msg[2]=='u')&&msg[3]==' '){
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
    if(!r->reading){
      if(y>=50&&y<50+LIB_ROWS*34){
        unsigned first=r->selected>=LIB_ROWS?r->selected-(LIB_ROWS-1):0;
        unsigned choice=first+(unsigned)(y-50)/34u;
        if(choice<r->book_count){
          if(choice==r->selected)open_selected(r);
          else r->selected=choice;
        }
      }
      return 1;
    }
    if(y<34&&x>=235){r->tone_open=1;return 1;}
    move(r,x<SCREEN_W/2?-1:1);return 1;
  }
  return 0;
}
