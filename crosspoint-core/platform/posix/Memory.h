#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
// POSIX stand-in for FreeInk's allocation helper. Supports both bounded
// arrays and constructed C++ types, preserving nothrow allocation semantics.
template <class T>
std::enable_if_t<std::is_array<T>::value, std::unique_ptr<T>>
makeUniqueNoThrow(std::size_t n) {
  using E = typename std::remove_extent<T>::type;
  return std::unique_ptr<T>(new (std::nothrow) E[n]);
}
template <class T, class... Args>
std::enable_if_t<!std::is_array<T>::value, std::unique_ptr<T>>
makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}
