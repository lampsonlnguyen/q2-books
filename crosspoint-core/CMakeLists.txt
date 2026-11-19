cmake_minimum_required(VERSION 3.16)
project(crosspod_crosspoint_core LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep original upstream CrossPoint UTF-8 code unmodified.
add_library(crosspoint_utf8 STATIC
    lib/Utf8/Utf8.cpp
)
target_include_directories(crosspoint_utf8 PUBLIC lib/Utf8)

enable_testing()
add_executable(crosspoint_core_tests tests/utf8_test.cpp)
target_link_libraries(crosspoint_core_tests PRIVATE crosspoint_utf8)
add_test(NAME crosspoint_unicode COMMAND crosspoint_core_tests)

add_executable(crosspoint_controls_tests tests/controls_test.cpp)
add_test(NAME q2_wheel_touch_mapping COMMAND crosspoint_controls_tests)

add_executable(crosspoint_display_tests tests/display_test.cpp)
add_test(NAME q2_lcd_color_adapter COMMAND crosspoint_display_tests)

# CrossPoint's original TXT-to-XHTML pipeline, compiled unchanged.
# POSIX shims replace its Arduino Print/SDK allocation and filesystem helpers.
add_library(crosspoint_txt STATIC lib/Txt/TxtToHtml.cpp)
target_include_directories(crosspoint_txt PUBLIC
    lib/Txt
    platform/posix
)
add_executable(crosspoint_txt_tests tests/txt_to_html_test.cpp)
target_link_libraries(crosspoint_txt_tests PRIVATE crosspoint_txt)
add_test(NAME crosspoint_vietnamese_txt COMMAND crosspoint_txt_tests)

# The original CrossPoint EPUB container.xml parser. The XML parser itself is
# Expat (system library on Linux), not the ESP32 toolchain or Q2 Pod EPUB code.
find_package(EXPAT REQUIRED)
add_library(crosspoint_epub_container STATIC lib/Epub/parsers/ContainerParser.cpp)
target_include_directories(crosspoint_epub_container PUBLIC
    lib/Epub/parsers
    lib/XmlParserUtils
    platform/posix
)
target_link_libraries(crosspoint_epub_container PUBLIC EXPAT::EXPAT)
add_executable(crosspoint_epub_container_tests tests/container_test.cpp)
target_link_libraries(crosspoint_epub_container_tests PRIVATE crosspoint_epub_container)
add_test(NAME crosspoint_epub_container COMMAND crosspoint_epub_container_tests)
