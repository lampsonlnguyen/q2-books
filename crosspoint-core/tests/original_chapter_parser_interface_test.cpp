// SPDX-License-Identifier: MIT
// Compile the original upstream chapter parser interface with Q2/POSIX headers.
// This does NOT exercise the implementation or claim EPUB layout feature parity.
#include <Page.h>
#include <Epub/Epub/parsers/ChapterHtmlSlimParser.h>
#include <type_traits>
static_assert(std::is_class_v<ChapterHtmlSlimParser>);
static_assert(sizeof(ChapterHtmlSlimParser)>0);
int main() { return 0; }
