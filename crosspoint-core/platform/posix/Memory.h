#pragma once
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
template<class T>
std::unique_ptr<T> makeUniqueNoThrow(std::size_t n) {
    using E=typename std::remove_extent<T>::type;
    return std::unique_ptr<T>(new(std::nothrow) E[n]);
}
