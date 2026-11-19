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
  memcpy(reader.book,"test line\nsecond line",21);
  reader.book_len=21;
  reader_index(&reader);
  ensure(reader.line_count==2,"TXT indexing");
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
