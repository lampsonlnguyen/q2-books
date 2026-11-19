cmake_minimum_required(VERSION 3.16)
project(crosspod_q2_platform LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(crosspod_platform STATIC Q2SocketInput.cpp Q2Framebuffer.cpp)
target_include_directories(crosspod_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Standalone hardware diagnostic: intentionally NOT named "crosspod" so the
# native Books launcher cannot mistake it for an EPUB reader.
add_executable(crosspod-hwcheck crosspod_hwcheck.cpp)
target_link_libraries(crosspod-hwcheck PRIVATE crosspod_platform)
find_package(Threads REQUIRED)
target_link_libraries(crosspod-hwcheck PRIVATE Threads::Threads)
