// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <string_view>
#include <functional>

namespace books {
using ImportedImageReader=std::function<bool(size_t,std::string&)>;
// Import decoded ebook data without granting access to external image files.
std::string packageImportedBook(const std::string& source,std::string_view title,
    std::string_view raw,bool html,const std::string& cacheDir,std::string& error,
    std::string_view author={},std::string_view language={},
    const ImportedImageReader& imageReader={},bool legacyMobi=false,const std::string& target={});
std::string packageHtmlBook(const std::string& source,const std::string& cacheDir,std::string& error);
}
