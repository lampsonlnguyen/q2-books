// SPDX-License-Identifier: MIT
#pragma once
// What Reading settings -> About and `books-reader --about` show.
#ifndef BOOKS_VERSION
#define BOOKS_VERSION "dev"
#endif

namespace books {
inline constexpr const char* kAboutLines[] = {
    "An ebook reader for Q2 Pod",
    "on the Shanling Q2.",
    "",
    "A port of CrossPoint Reader:",
    "its EPUB engine, layout and",
    "fonts, on the Q2's Linux.",
    "github.com/crosspoint-reader",
    "",
    "github.com/lampsonlnguyen/q2-books",
    "MIT license; see LICENSE files",
};
inline constexpr const char* kAboutText =
    "An ebook reader for Q2 Pod on the Shanling Q2.\n"
    "A port of CrossPoint Reader (github.com/crosspoint-reader/crosspoint-reader):\n"
    "its EPUB engine, layout and fonts, running on the Q2's Linux.\n"
    "Source: github.com/lampsonlnguyen/q2-books (MIT; see the LICENSE files).";
}  // namespace books
