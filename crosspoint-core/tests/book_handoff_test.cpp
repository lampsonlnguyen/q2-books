#include <cassert>
#include <string>
#include "../platform/q2/Q2BookHandoff.hpp"
int main(){
 using namespace books;
 auto epub=parse_q2_book("/mnt/mmc/Books/Tiếng Việt/Đà Lạt.EPUB");
 assert(epub && epub.format==BookFormat::Epub);
 assert(epub.path=="/mnt/mmc/Books/Tiếng Việt/Đà Lạt.EPUB");
 auto txt=parse_q2_book("/mnt/mmc/Books/Notes.txt");
 assert(txt && txt.format==BookFormat::Txt);
 assert(!parse_q2_book("/mnt/mmc/BooksOld/x.epub"));
 assert(!parse_q2_book("/mnt/mmc/Books/../private.epub"));
 assert(!parse_q2_book("/mnt/mmc/Books/a//b.epub"));
 assert(!parse_q2_book("/mnt/mmc/Books/book.mobi"));
 assert(!parse_q2_book("/mnt/mmc/Books/a.txt/"));
 assert(!parse_q2_book("/mnt/mmc/Books/./b.epub"));
}
