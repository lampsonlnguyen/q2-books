/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef CROSSPOD_H
#define CROSSPOD_H
#include <stdint.h>
enum { SCREEN_W=375, SCREEN_H=320, BOOK_MAX=262144, LINE_MAX=12000, PAGE_LINES=13, COLS=38, LIB_MAX=128, PATH_MAX=192, LIB_ROWS=7 };
typedef uint32_t u32;
typedef struct {
  char book[BOOK_MAX+1];
  unsigned starts[LINE_MAX];
  unsigned line_count, book_len, page;
  int wheel_acc;
  int quit, loaded, touch_down, center_down;
  char paths[LIB_MAX][PATH_MAX]; /* paths relative to /mnt/mmc/Books */
  unsigned book_count, selected;
  int reading, open_request, book_error; /* open_request is entry index + 1 */
  int warmth, night, tone_open, tone_dirty; /* warmth: 0 cool to 100 warm */
} Reader;
void reader_init(Reader *r);
void reader_index(Reader *r);
int reader_catalog_add(Reader *r,const char *relative);
void reader_catalog_sort(Reader *r);
void reader_open_result(Reader *r,int ok);
int reader_input(Reader *r,const char *msg);
void reader_render(const Reader *r,u32 *frame,unsigned stride);
#endif
