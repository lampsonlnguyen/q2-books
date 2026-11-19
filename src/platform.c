/* SPDX-License-Identifier: GPL-3.0-or-later
 * Q2 card runtime using the Linux/MIPS system-call ABI and framebuffer contract.
 * Startup/syscall pattern adapted from DiamondBond's Buddy/q2-fidget card.
 */
#include "crosspod.h"
long sc(long n,long a,long b,long c,long d,long e,long f);
__asm__(".text\n.set noreorder\n.globl sc\nsc:\n"
        "move $2, $4\nmove $4, $5\nmove $5, $6\nmove $6, $7\n"
        "lw $7, 16($29)\nlw $8, 20($29)\nlw $9, 24($29)\n"
        "addiu $29, $29, -32\nsw $8, 16($29)\nsw $9, 20($29)\n"
        "syscall\naddiu $29, $29, 32\n"
        "beqz $7, 1f\nnop\nsubu $2, $0, $2\n1: jr $31\nnop\n"
        ".globl __start\n__start:\n"
        "lw $4, 0($29)\naddiu $5, $29, 4\nli $8, -8\nand $29, $29, $8\n"
        "addiu $29, $29, -32\njal q2_main\nnop\nmove $4, $2\nli $2, 4246\nsyscall\n1: b 1b\nnop\n.set reorder\n");

enum { SYS_READ=4003,SYS_WRITE=4004,SYS_OPEN=4005,SYS_CLOSE=4006,
       SYS_UNLINK=4010,SYS_LSEEK=4019,SYS_RENAME=4038,SYS_IOCTL=4054,SYS_MMAP=4090,
       SYS_MUNMAP=4091,SYS_FSYNC=4118,SYS_BIND=4169,SYS_RECV=4175,
       SYS_SOCKET=4183,SYS_POLL=4188,SYS_CLOCK_GETTIME=4263,
       FB_VSCREEN=0x4600,FB_FSCREEN=0x4602,FB_PAN=0x4606,
       O_WRONLY=1,O_RDWR=2,O_CREAT=0x100,O_TRUNC=0x200,O_CLOEXEC=0x80000,
       MSG_DONTWAIT=0x40,FRAME_MS=17 };
#define INPUT_SOCKET "/tmp/q2-crosspod.input.sock"
/* Diagnostic file on the card: helps distinguish launch failure from display failure.
 * Rewritten at each launch; tiny, one-shot writes (no writes in animation loop).
 * Remove after real-device startup is confirmed.
 */
#define DIAGNOSTIC "/mnt/mmc/.crosspod/diagnostic.txt"
static int diag_fd=-1;
static void diag(const char *msg) {
    if(diag_fd<0)return;
    unsigned n=0;
    while(msg[n])n++;
    sc(SYS_WRITE,diag_fd,(long)msg,n,0,0,0);
}
static void diag_end(void) {
    if(diag_fd>=0){sc(SYS_CLOSE,diag_fd,0,0,0,0,0);diag_fd=-1;}
}
void *memset(void *dst,int v,__SIZE_TYPE__ n) {
    unsigned char *p=dst;
    while(n--)*p++=(unsigned char)v;
    return dst;
}
void *memcpy(void *dst,const void *src,__SIZE_TYPE__ n) {
    unsigned char *p=dst;
    const unsigned char *q=src;
    while(n--)*p++=*q++;
    return dst;
}
static Reader app;
/* Full-size software backbuffer: draw off-screen, then publish only finished
 * frames. The Q2's fb pan operation blackens the panel on physical hardware. */
static u32 frame[SCREEN_W*SCREEN_H];
static void present_frame(u32 *dst,unsigned stride) {
    /* Physical fb memory is portrait with landscape pixels rotated clockwise.
     * Write in the framebuffer's contiguous memory order for a short copy. */
    for(unsigned x=0;x<SCREEN_W;x++) {
        u32 *target=dst+x*stride;
        const u32 *source=frame+x*SCREEN_H;
        for(unsigned y=0;y<SCREEN_H;y++)target[y]=source[y];
    }
}

int q2_read_at(int fd,unsigned offset,void *dest,unsigned n) {
    long where=sc(SYS_LSEEK,fd,(long)offset,0,0,0,0);
    if(where!=(long)offset)return -1;
    unsigned done=0;
    while(done<n){
        long got=sc(SYS_READ,fd,(long)((unsigned char*)dest+done),n-done,0,0,0);
        if(got<=0)break;
        done+=(unsigned)got;
    }
    return (int)done;
}
int epub_load(Reader *r,int fd,unsigned length);
static int bind_input(void) {
    struct { unsigned short family; char path[108]; } addr={1,INPUT_SOCKET};
    int fd=(int)sc(SYS_SOCKET,1,1,0,0,0,0);
    if(fd<0)return -1;
    sc(SYS_UNLINK,(long)INPUT_SOCKET,0,0,0,0,0);
    if(!sc(SYS_BIND,fd,(long)&addr,sizeof addr,0,0,0))return fd;
    sc(SYS_CLOSE,fd,0,0,0,0,0);
    return -1;
}
int q2_main(void) {
    diag_fd=(int)sc(SYS_OPEN,(long)DIAGNOSTIC,O_WRONLY|O_CREAT|O_TRUNC|O_CLOEXEC,0644,0,0,0);
    diag("START: CrossPod entry reached\\n");
    unsigned var[40],fix[17];
    int fb=(int)sc(SYS_OPEN,(long)"/dev/fb0",O_RDWR|O_CLOEXEC,0,0,0,0);
    if(fb<0){diag("ERROR: opening /dev/fb0\\n");diag_end();return 1;}
    diag("OK: framebuffer opened\\n");
    if(sc(SYS_IOCTL,fb,FB_VSCREEN,(long)var,0,0,0) ||
       sc(SYS_IOCTL,fb,FB_FSCREEN,(long)fix,0,0,0)) {
        diag("ERROR: framebuffer ioctl\\n");diag_end();
        sc(SYS_CLOSE,fb,0,0,0,0,0);return 1;
    }
    unsigned stride=fix[11]/sizeof(u32),front=var[5];
    if(var[0]!=SCREEN_H||var[1]!=SCREEN_W||var[4]||var[6]!=32||
       var[8]!=16||fix[11]%sizeof(u32)||stride<SCREEN_H) {
        diag("ERROR: unsupported framebuffer geometry or pixel layout\\n");diag_end();
        sc(SYS_CLOSE,fb,0,0,0,0,0);return 1;
    }
    unsigned rows=fix[5]/fix[11];
    if(rows>var[3])rows=var[3];
    if(rows<SCREEN_W||front>rows-SCREEN_W) {
        diag("ERROR: framebuffer stride/pages invalid\\n");diag_end();
        sc(SYS_CLOSE,fb,0,0,0,0,0);return 1;
    }
    long mapped=sc(SYS_MMAP,0,(long)fix[5],3,1,fb,0);
    if(mapped<0 && mapped>=-4095) {
        diag("ERROR: mmap framebuffer failed\\n");diag_end();
        sc(SYS_CLOSE,fb,0,0,0,0,0);return 1;
    }
    u32 *pixels=(u32*)mapped;
    int input=bind_input();
    if(input<0) {
        diag("ERROR: reader input socket bind failed\\n");diag_end();
        sc(SYS_MUNMAP,mapped,(long)fix[5],0,0,0,0);
        sc(SYS_CLOSE,fb,0,0,0,0,0);return 1;
    }
    reader_init(&app);
    int file=(int)sc(SYS_OPEN,(long)"/mnt/mmc/.crosspod/book.epub",0,0,0,0,0);
    if(file>=0) {
        long size=sc(SYS_LSEEK,file,0,2,0,0,0);
        if(size>0&&size<=0x7fffffffl&&epub_load(&app,file,(unsigned)size)>0) {
            app.loaded=1;
            diag("OK: EPUB opened and XHTML chapters decoded\\n");
        } else {
            diag("ERROR: EPUB parse failed\\n");
        }
        sc(SYS_CLOSE,file,0,0,0,0,0);
    }
    if(!app.loaded){
        app.book_len=0;
        file=(int)sc(SYS_OPEN,(long)"/mnt/mmc/.crosspod/book.txt",0,0,0,0,0);
        if(file>=0){
            long total=0;
            while(total<BOOK_MAX){
                long n=sc(SYS_READ,file,(long)(app.book+total),BOOK_MAX-total,0,0,0);
                if(n<=0)break;
                total+=n;
            }
            app.book_len=(unsigned)total;
            app.book[total]=0;
            app.loaded=1;
            reader_index(&app);
            sc(SYS_CLOSE,file,0,0,0,0,0);
            diag("OK: TXT loaded\\n");
        } else diag("ERROR: neither book.epub nor book.txt available\\n");
    }
    diag("OK: input socket bound and reader state initialized\\n");
    /* Paint the first menu to the currently visible framebuffer page.
     * This prevents initial black display if a dual-page pan is reported
     * as successful but is not displayed by the device driver. */
    reader_render(&app,frame,SCREEN_H);
    present_frame(pixels+front*stride,stride);
    diag("OK: first complete frame copied to visible framebuffer\\n");
    diag_end();
    unsigned visible=front;
    struct {int fd;short events,revents;} poll_input={input,1,0};
    while(!app.quit) {
        long ready=sc(SYS_POLL,(long)&poll_input,1,-1,0,0,0);
        if(ready==-4)continue;
        if(ready<0||!(poll_input.revents&1))break;
        char message[64];
        long n=sc(SYS_RECV,input,(long)message,sizeof message-1,MSG_DONTWAIT,0,0);
        if(n>0) {
            message[n]=0;
            if(reader_input(&app,message)){
                reader_render(&app,frame,SCREEN_H);
                present_frame(pixels+visible*stride,stride);
            }
        }
    }
    sc(SYS_CLOSE,input,0,0,0,0,0);
    sc(SYS_UNLINK,(long)INPUT_SOCKET,0,0,0,0,0);
    sc(SYS_MUNMAP,mapped,(long)fix[5],0,0,0,0);
    sc(SYS_CLOSE,fb,0,0,0,0,0);
    return app.quit?0:1;
}
