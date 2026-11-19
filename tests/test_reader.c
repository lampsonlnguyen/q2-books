/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crosspod.h"
static FILE *zipfile;
int q2_read_at(int fd,unsigned offset,void* dest,unsigned n){
  (void)fd;
  if(fseek(zipfile,(long)offset,SEEK_SET)!=0)return -1;
  return (int)fread(dest,1,n,zipfile);
}
int epub_load(Reader *r,int fd,unsigned length);
static Reader reader;
static void ensure(int yes,const char *what){
  if(!yes){fprintf(stderr,"FAILED: %s\n",what);exit(1);}
}
int main(int argc,char **argv){
  ensure(argc==2,"fixture path");
  reader_init(&reader);
  ensure(reader.warmth==35 && reader.night==0,"default reading tone");
  ensure(reader_catalog_add(&reader,"A Book.TXT"),"catalog TXT");
  ensure(reader_catalog_add(&reader,"Fantasy/Another.EPUB"),"catalog EPUB nested");
  ensure(reader_catalog_add(&reader,"z-last.txt"),"catalog third");
  ensure(!reader_catalog_add(&reader,"../escape.epub"),"reject traversal");
  ensure(!reader_catalog_add(&reader,"a.jpg"),"reject other formats");
  ensure(!reader_catalog_add(&reader,"A Book.TXT"),"deduplicate");
  reader_catalog_sort(&reader);
  ensure(reader.book_count==3 && strcmp(reader.paths[0],"A Book.TXT")==0,"sorted catalogue");
  ensure(reader_input(&reader,"k d 218")==1 && reader.open_request==1,"open first book");
  ensure(reader_input(&reader,"k u 218")==0,"release centre");
  reader_open_result(&reader,1);
  ensure(reader.reading && reader.loaded,"enter book reader");
  ensure(reader_input(&reader,"t d 260 12")==1 && reader.tone_open,"open tone menu");
  ensure(reader_input(&reader,"t u 260 12")==0,"touch release");
  ensure(reader_input(&reader,"t d 329 150")==1 && reader.warmth==100,"warm preset by touch");
  ensure(reader_input(&reader,"t u 329 150")==0,"touch release 2");
  ensure(reader_input(&reader,"t d 45 150")==1 && reader.warmth==0,"cool preset by touch");
  ensure(reader_input(&reader,"k d 218")==1 && reader.night,"toggle night");
  ensure(reader_input(&reader,"k u 218")==0,"release centre");
  ensure(reader_input(&reader,"k u 170")==1 && !reader.tone_open && !reader.quit,"back closes settings");
  ensure(reader_input(&reader,"k u 170")==1 && !reader.reading && !reader.quit,"back returns to book list");
  ensure(reader_input(&reader,"k u 170")==1 && reader.quit,"back exits from book list");
  reader_init(&reader);
  memcpy(reader.book,"test line\nsecond line",21);
  reader.book_len=21;
  reader_index(&reader);
  ensure(reader.line_count==2,"TXT indexing");
  reader_init(&reader);
  /* 38 Vietnamese glyphs per line; the page boundary must never split UTF-8. */
  const char *vn="Tiếng Việt có dấu: Đà Lạt, Nguyễn, Trường, ươn ă â ê ô ơ ư.";
  memcpy(reader.book,vn,strlen(vn)+1);
  reader.book_len=(unsigned)strlen(vn);
  reader_index(&reader);
  ensure(reader.line_count>=2,"Vietnamese UTF-8 codepoint wrapping");
  for(unsigned i=0;i<reader.line_count;i++)
    ensure((((unsigned char)reader.book[reader.starts[i]])&0xc0)!=0x80,"no UTF-8 continuation at line start");
  zipfile=fopen(argv[1],"rb");
  ensure(zipfile!=NULL,"open EPUB fixture");
  ensure(fseek(zipfile,0,SEEK_END)==0,"seek EPUB");
  long length=ftell(zipfile);
  ensure(length>0,"size EPUB");
  reader_init(&reader);
  ensure(epub_load(&reader,1,(unsigned)length)==2,"extract both EPUB chapters");
  ensure(strstr(reader.book,"First chapter")!=NULL,"chapter one content");
  ensure(strstr(reader.book,"Second chapter")!=NULL,"chapter two content");
  ensure(strstr(reader.book,"hello & world")!=NULL,"HTML entity");
  ensure(reader.line_count>0,"EPUB pagination");
  fclose(zipfile);
  puts("TXT and EPUB parser smoke tests passed");
  return 0;
}
