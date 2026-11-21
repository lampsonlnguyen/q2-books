// SPDX-License-Identifier: MIT
// Shanling Q2 libstdc++ predates floating-point std::from_chars (GLIBCXX_3.4.29).
// Implement the one missing C++17 overload with bounded stack storage. The
// integer overloads remain untouched. Locale is the process' C numeric locale.
#include <charconv>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <system_error>
#include <new>

namespace std {
from_chars_result from_chars(const char* first, const char* last,
                             float& result, chars_format format) noexcept {
  if (!first || !last || last < first)
    return {first, errc::invalid_argument};
  const size_t bytes = static_cast<size_t>(last - first);
  if (!bytes || bytes > 256 || *first == '+' || *first == ' ' || *first == '\t')
    return {first, errc::invalid_argument};
  char input[260];
  const bool hex = format == chars_format::hex;
  const size_t prefix = hex ? 2 : 0;
  if (hex) { input[0] = '0'; input[1] = 'x'; }
  std::memcpy(input + prefix, first, bytes);
  input[prefix + bytes] = '\0';

  errno = 0;
  char* stop = nullptr;
  const float parsed = std::strtof(input, &stop);
  const ptrdiff_t used = stop - (input + prefix);
  if (used <= 0)
    return {first, errc::invalid_argument};
  // scientific requires an exponent; fixed disallows exponent consumption.
  if (format == chars_format::scientific) {
    bool exponent = false;
    for (ptrdiff_t i = 0; i < used; ++i)
      if (first[i] == 'e' || first[i] == 'E') exponent = true;
    if (!exponent) return {first, errc::invalid_argument};
  }
  if (format == chars_format::fixed) {
    for (ptrdiff_t i = 0; i < used; ++i)
      if (first[i] == 'e' || first[i] == 'E')
        return {first + i, errc{}};
  }
  if (errno == ERANGE)
    return {first + used, errc::result_out_of_range};
  result = parsed;
  return {first + used, errc{}};
}
} // namespace std

// GCC 12 headers emit this helper (GLIBCXX_3.4.29); the Q2 C++ runtime
// predates it but already supports the standard exception type.
namespace std {
void __throw_bad_array_new_length() {
  throw bad_array_new_length();
}
}
