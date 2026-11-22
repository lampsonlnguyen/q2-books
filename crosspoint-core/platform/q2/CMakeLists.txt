cmake_minimum_required(VERSION 3.16)
project(books_q2_platform LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(books_platform STATIC Q2SocketInput.cpp Q2Framebuffer.cpp)
target_include_directories(books_platform PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(books_platform PUBLIC Threads::Threads)

# Standalone hardware diagnostic: intentionally NOT named "books" so the
# native Books launcher cannot mistake it for an EPUB reader.
add_executable(books-hwcheck books_hwcheck.cpp)
target_link_libraries(books-hwcheck PRIVATE books_platform)
find_package(Threads REQUIRED)
target_link_libraries(books-hwcheck PRIVATE Threads::Threads)

# True CrossPoint source components, not substitute EPUB text layout.
# These targets compile for the Q2's MIPS32 Linux architecture without any
# Arduino/ESP-IDF dependency or host-only Expat (XML lives in its own module).
set(CROSSPOINT_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_library(books_upstream_text STATIC
    "${CROSSPOINT_CORE_ROOT}/lib/Utf8/Utf8.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/WordStore.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/css/CssParser.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/CjkBreaks.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/hyphenation/HyphenationCommon.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/EpdFont/EpdFont.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/EpdFont/EpdFontFamily.cpp"
    "${CROSSPOINT_CORE_ROOT}/lib/MiniBidi/minibidi.c"
    "${CROSSPOINT_CORE_ROOT}/lib/MiniBidi/BidiUtils.cpp"
)
target_include_directories(books_upstream_text PUBLIC
    "${CROSSPOINT_CORE_ROOT}/platform/posix"
    "${CROSSPOINT_CORE_ROOT}/lib/Utf8"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/css"
    "${CROSSPOINT_CORE_ROOT}/lib/EpdFont"
    "${CROSSPOINT_CORE_ROOT}/lib/MiniBidi"
)
target_link_libraries(books_upstream_text PUBLIC Threads::Threads)

# This is NOT an EPUB reader. It checks actual original CrossPoint Unicode,
# CSS, CJK and BiDi algorithms under Linux/MIPS, with no display access.
add_executable(books-engine-smoke books_engine_smoke.cpp)
target_link_libraries(books-engine-smoke PRIVATE books_upstream_text)

# Explicit opt-in page-only Q2 LCD diagnostic. Not the production EPUB reader.
add_executable(books-page-demo books_page_demo.cpp)
target_link_libraries(books-page-demo PRIVATE books_platform Threads::Threads)
