cmake_minimum_required(VERSION 3.16)
project(crosspod_q2_platform LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
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

# True CrossPoint source components, not substitute EPUB text layout.
# These targets compile for the Q2's MIPS32 Linux architecture without any
# Arduino/ESP-IDF dependency or host-only Expat (XML lives in its own module).
set(CROSSPOINT_CORE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
add_library(crosspod_upstream_text STATIC
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
target_include_directories(crosspod_upstream_text PUBLIC
    "${CROSSPOINT_CORE_ROOT}/platform/posix"
    "${CROSSPOINT_CORE_ROOT}/lib/Utf8"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub"
    "${CROSSPOINT_CORE_ROOT}/lib/Epub/css"
    "${CROSSPOINT_CORE_ROOT}/lib/EpdFont"
    "${CROSSPOINT_CORE_ROOT}/lib/MiniBidi"
)
target_link_libraries(crosspod_upstream_text PUBLIC Threads::Threads)

# This is NOT an EPUB reader. It checks actual original CrossPoint Unicode,
# CSS, CJK and BiDi algorithms under Linux/MIPS, with no display access.
add_executable(crosspod-engine-smoke crosspod_engine_smoke.cpp)
target_link_libraries(crosspod-engine-smoke PRIVATE crosspod_upstream_text)

# Explicit opt-in page-only Q2 LCD diagnostic. Not the production EPUB reader.
add_executable(crosspod-page-demo crosspod_page_demo.cpp)
target_link_libraries(crosspod-page-demo PRIVATE crosspod_platform Threads::Threads)
