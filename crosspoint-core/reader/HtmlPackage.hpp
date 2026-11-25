// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>

namespace books {
// Import decoded ebook data without granting access to external image files.
std::string packageImportedBook(const std::string& source,std::string_view title,
    std::string_view raw,bool html,const std::string& cacheDir,std::string& error);
std::string packageHtmlBook(const std::string& source,const std::string& cacheDir,std::string& error);
}
