// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace books {
std::string packageMarkdownBook(const std::string& path, const std::string& cacheDir,
                                std::string& error);
}
