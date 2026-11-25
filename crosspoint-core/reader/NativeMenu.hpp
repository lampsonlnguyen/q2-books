// SPDX-License-Identifier: MIT
#pragma once
#include <csignal>
#include <string>
#include <string_view>

namespace books {
inline constexpr std::string_view kNativeHome="home";
inline constexpr std::string_view kNativeResume="resume";
inline constexpr std::string_view kNativeBrowse="browse";
inline constexpr std::string_view kNativeOptions="options";

std::string nativeMenuReply(std::string_view request,bool canOpen,bool canResume);
int serveNativeMenu(const volatile std::sig_atomic_t& interrupted);
}
