// SPDX-License-Identifier: MIT
// Safe bounded JPEG grayscale decoder for an EPUB image. The libjpeg setjmp
// error boundary stays entirely in C: no C++ destructors cross a longjmp.
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <jpeglib.h>

struct BooksJpegError {
  struct jpeg_error_mgr base;
  jmp_buf jump;
};
static void booksJpegError(j_common_ptr cinfo) {
  struct BooksJpegError* err =
      (struct BooksJpegError*)cinfo->err;
  longjmp(err->jump,1);
}
// Returns a malloc-owned grayscale or RGB buffer (must be freed by caller).
static int decodeJpeg(const unsigned char* bytes,size_t length,
                       unsigned char** pixels,unsigned* width,unsigned* height,int color,int thumbnail) {
  if(!bytes||!pixels||!width||!height||length<16||length>4194304)return 0;
  *pixels=NULL;*width=0;*height=0;
  struct jpeg_decompress_struct cinfo;
  struct BooksJpegError err;
  unsigned char* volatile image=NULL;
  cinfo.err=jpeg_std_error(&err.base);
  err.base.error_exit=booksJpegError;
  if(setjmp(err.jump)) {
    if(image)free((void*)image);
    jpeg_destroy_decompress(&cinfo);
    return 0;
  }
  jpeg_create_decompress(&cinfo);
  cinfo.mem->max_memory_to_use=12L*1024*1024;
  jpeg_mem_src(&cinfo,(const unsigned char*)bytes,(unsigned long)length);
  if(jpeg_read_header(&cinfo,TRUE)!=JPEG_HEADER_OK) {
    jpeg_destroy_decompress(&cinfo);return 0;
  }
  const unsigned sourceLimit=thumbnail?16777216u:1048576u;
  if(!cinfo.image_width||!cinfo.image_height||
     cinfo.image_width>sourceLimit/cinfo.image_height) {
    jpeg_destroy_decompress(&cinfo);return 0;
  }
  if(thumbnail) {
    cinfo.scale_num=1;
    cinfo.scale_denom=1;
    while(cinfo.scale_denom<8 &&
          (cinfo.image_width>384u*cinfo.scale_denom || cinfo.image_height>512u*cinfo.scale_denom))
      cinfo.scale_denom*=2;
  }
  cinfo.out_color_space=color?JCS_RGB:JCS_GRAYSCALE;
  const unsigned channels=color?3u:1u;
  if(!jpeg_start_decompress(&cinfo)) {
    jpeg_destroy_decompress(&cinfo);return 0;
  }
  if((unsigned)cinfo.output_components!=channels || !cinfo.output_width ||
     !cinfo.output_height ||
     cinfo.output_width>1048576u/cinfo.output_height) {
    jpeg_destroy_decompress(&cinfo);return 0;
  }
  const size_t n=(size_t)cinfo.output_width*cinfo.output_height*channels;
  image=(unsigned char*)malloc(n);
  if(!image) {
    jpeg_destroy_decompress(&cinfo);return 0;
  }
  while(cinfo.output_scanline<cinfo.output_height) {
    JSAMPROW row=(JSAMPROW)(image+(size_t)cinfo.output_scanline*cinfo.output_width*channels);
    if(jpeg_read_scanlines(&cinfo,&row,1)!=1) {
      free((void*)image);jpeg_destroy_decompress(&cinfo);return 0;
    }
  }
  const unsigned w=cinfo.output_width,h=cinfo.output_height;
  if(!jpeg_finish_decompress(&cinfo)) {
    free((void*)image);jpeg_destroy_decompress(&cinfo);return 0;
  }
  jpeg_destroy_decompress(&cinfo);
  *pixels=(unsigned char*)image;*width=w;*height=h;
  return 1;
}

int booksDecodeJpeg(const unsigned char* bytes,size_t length,
                    unsigned char** pixels,unsigned* width,unsigned* height) {
  return decodeJpeg(bytes,length,pixels,width,height,0,0);
}
int booksDecodeJpegRgb(const unsigned char* bytes,size_t length,
                       unsigned char** pixels,unsigned* width,unsigned* height) {
  return decodeJpeg(bytes,length,pixels,width,height,1,0);
}

int booksDecodeJpegCover(const unsigned char* bytes,size_t length,
                         unsigned char** pixels,unsigned* width,unsigned* height) {
  return decodeJpeg(bytes,length,pixels,width,height,1,1);
}
