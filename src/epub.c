/* SPDX-License-Identifier: GPL-3.0-or-later
 * Small, limited EPUB ZIP/XHTML reader. ZIP order is used, not OPF spine order.
 * The 3rd party DEFLATE implementation is licensed in src/sinfl.h.
 */
#include "crosspod.h"
/* Freestanding MIPS build provides these routines in platform.c. */
void *memcpy(void *dest,const void *src,__SIZE_TYPE__ n);
void *memset(void *dest,int value,__SIZE_TYPE__ n);
#define SINFL_NO_SIMD
#define SINFL_IMPLEMENTATION
#include "sinfl.h"
enum { ZIP_IN_MAX=524288, ZIP_OUT_MAX=262144, ZIP_TAIL=65557 };
static unsigned char tail[ZIP_TAIL], compressed[ZIP_IN_MAX+32], uncompressed[ZIP_OUT_MAX+32];
int q2_read_at(int fd,unsigned offset,void *dest,unsigned n);
static unsigned rd16(const unsigned char*p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static unsigned rd32(const unsigned char*p){return rd16(p)|(rd16(p+2)<<16);}
static int match_ext(const unsigned char *s,unsigned n){
    if(n<5)return 0;
    const unsigned char *p=s+n-5;
    if(p[0]=='.'&&(p[1]=='h'||p[1]=='H')&&(p[2]=='t'||p[2]=='T')&&(p[3]=='m'||p[3]=='M')&&(p[4]=='l'||p[4]=='L'))return 1;
    if(n>=6) {
        p=s+n-6;
        if(p[0]=='.'&&(p[1]=='x'||p[1]=='X')&&(p[2]=='h'||p[2]=='H')&&(p[3]=='t'||p[3]=='T')&&(p[4]=='m'||p[4]=='M')&&(p[5]=='l'||p[5]=='L'))return 1;
    }
    return 0;
}
static int isspace_c(unsigned char c){return c==' '||c=='\r'||c=='\n'||c=='\t';}
static int lower(int ch){return ch>='A'&&ch<='Z'?ch+32:ch;}
static int starts(const unsigned char *p,unsigned n,const char*name){
    unsigned i=0;
    for(;name[i];i++){if(i>=n||lower(p[i])!=name[i])return 0;}
    return 1;
}
static void add(Reader*r,char c){
    if(r->book_len>=BOOK_MAX)return;
    if(c=='\n'&&(!r->book_len||r->book[r->book_len-1]=='\n'))return;
    r->book[r->book_len++]=c;
}
static void newline(Reader*r){
    if(r->book_len&&r->book[r->book_len-1]!='\n')add(r,'\n');
}
static void append_html(Reader*r,const unsigned char*buf,unsigned len){
    int in_tag=0, hidden=0, in_body=0, seen_body=0;
    for(unsigned i=0;i<len&&r->book_len<BOOK_MAX-64;i++){
        unsigned char c=buf[i];
        if(c=='<'){
            unsigned j=i+1,closing=0;
            if(j<len&&buf[j]=='/'){closing=1;j++;}
            unsigned start=j;
            while(j<len&&((buf[j]>='a'&&buf[j]<='z')||(buf[j]>='A'&&buf[j]<='Z')||(buf[j]>='0'&&buf[j]<='9')))j++;
            unsigned n=j-start;
            if(starts(buf+start,n,"body")){seen_body=1;in_body=!closing;}
            if(starts(buf+start,n,"script")||starts(buf+start,n,"style")||starts(buf+start,n,"head")||starts(buf+start,n,"svg")) {
                if(!closing)hidden++;else if(hidden)hidden--;
            }
            if(!hidden&&(!seen_body||in_body)){
                if(starts(buf+start,n,"p")||starts(buf+start,n,"div")||starts(buf+start,n,"br")||starts(buf+start,n,"h1")||starts(buf+start,n,"h2")||starts(buf+start,n,"h3")||starts(buf+start,n,"li")||starts(buf+start,n,"section"))newline(r);
            }
            in_tag=1;continue;
        }
        if(c=='>'){in_tag=0;continue;}
        if(in_tag||hidden||(seen_body&&!in_body))continue;
        if(c=='&') {
            if(i+5<=len&&starts(buf+i+1,len-i-1,"amp;")){add(r,'&');i+=4;continue;}
            if(i+4<=len&&starts(buf+i+1,len-i-1,"lt;")){add(r,'<');i+=3;continue;}
            if(i+4<=len&&starts(buf+i+1,len-i-1,"gt;")){add(r,'>');i+=3;continue;}
            if(i+6<=len&&starts(buf+i+1,len-i-1,"nbsp;")){add(r,' ');i+=5;continue;}
            if(i+6<=len&&starts(buf+i+1,len-i-1,"quot;")){add(r,'"');i+=5;continue;}
            if(i+5<=len&&starts(buf+i+1,len-i-1,"apos;")){add(r,'\'');i+=5;continue;}
        }
        if(c=='\n'||c=='\r'||c=='\t')c=' ';
        if(c==' '&&(!r->book_len||r->book[r->book_len-1]==' '||r->book[r->book_len-1]=='\n'))continue;
        add(r,(char)c);
    }
    newline(r);
}
static int unpack(int fd,unsigned local,unsigned method,unsigned clen,unsigned ulen,Reader*r) {
    unsigned char hdr[30];
    if(clen>ZIP_IN_MAX||ulen>ZIP_OUT_MAX||!ulen)return 0;
    if(q2_read_at(fd,local,hdr,sizeof hdr)!=(int)sizeof hdr||rd32(hdr)!=0x04034b50u)return 0;
    unsigned data=local+30+rd16(hdr+26)+rd16(hdr+28);
    if(q2_read_at(fd,data,compressed,clen)!=(int)clen)return 0;
    int produced=0;
    if(method==0){
        if(clen!=ulen)return 0;
        for(unsigned i=0;i<ulen;i++)uncompressed[i]=compressed[i];
        produced=(int)ulen;
    } else if(method==8){
        produced=sinflate(uncompressed,(int)ulen,compressed,(int)clen);
    }
    if(produced!=(int)ulen)return 0;
    append_html(r,uncompressed,ulen);
    return 1;
}
/* Returns number of XHTML documents decoded; -1 is unsupported/invalid ZIP. */
int epub_load(Reader*r,int fd,unsigned length){
    if(length<22)return -1;
    unsigned tail_len=length<ZIP_TAIL?length:ZIP_TAIL;
    unsigned tail_pos=length-tail_len;
    if(q2_read_at(fd,tail_pos,tail,tail_len)!=(int)tail_len)return -1;
    int end=-1;
    for(int i=(int)tail_len-22;i>=0;i--)
        if(rd32(tail+i)==0x06054b50u){end=i;break;}
    if(end<0)return -1;
    unsigned count=rd16(tail+end+10),central=rd32(tail+end+16);
    if(count>4096||central>=length)return -1;
    unsigned position=central,chapters=0;
    r->book_len=0;
    for(unsigned k=0;k<count&&r->book_len<BOOK_MAX-128;k++){
        unsigned char hdr[46];
        if(position>length-46||q2_read_at(fd,position,hdr,46)!=46||rd32(hdr)!=0x02014b50u)break;
        unsigned name_len=rd16(hdr+28),extra=rd16(hdr+30),comment=rd16(hdr+32);
        unsigned flags=rd16(hdr+8),method=rd16(hdr+10),clen=rd32(hdr+20),ulen=rd32(hdr+24),local=rd32(hdr+42);
        if(name_len>250||position+46+name_len+extra+comment<position)break;
        unsigned char name[256]={0};
        if(q2_read_at(fd,position+46,name,name_len)!=(int)name_len)break;
        position+=46+name_len+extra+comment;
        if((flags&1u)||!match_ext(name,name_len)||name_len==0)continue;
        if(unpack(fd,local,method,clen,ulen,r))chapters++;
    }
    r->book[r->book_len]=0;
    reader_index(r);
    return chapters?(int)chapters:-1;
}
