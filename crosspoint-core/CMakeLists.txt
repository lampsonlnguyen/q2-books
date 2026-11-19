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

# Q2 Pod keeps its existing Books shelf and supplies the selected path.
# CrossPoint uses this standalone contract rather than the game's app runtime.
add_executable(crosspoint_books_handoff_tests tests/book_handoff_test.cpp)
add_test(NAME q2_books_handoff COMMAND crosspoint_books_handoff_tests)

# Assertions must remain active in Release (-DNDEBUG would otherwise disable all tests).
foreach(t crosspoint_core_tests crosspoint_controls_tests crosspoint_display_tests
          crosspoint_txt_tests crosspoint_epub_container_tests crosspoint_books_handoff_tests)
    target_compile_options(${t} PRIVATE -UNDEBUG)
endforeach()

# CrossPoint ZIP reader: original upstream ZipFile source, Linux storage adapter
# and a bounded zlib streaming replacement for the ESP32 miniz implementation.
find_package(ZLIB REQUIRED)
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_library(crosspoint_zip STATIC
    lib/ZipFile/ZipFile.cpp
    platform/posix/InflateStream.cpp
)
target_include_directories(crosspoint_zip PUBLIC
    lib/ZipFile
    platform/posix
)
target_link_libraries(crosspoint_zip PUBLIC ZLIB::ZLIB)

set(EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/multilingual.epub")
add_custom_command(OUTPUT "${EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_epub_fixture.py" "${EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_epub_fixture.py"
    VERBATIM
)
add_custom_target(epub_fixture ALL DEPENDS "${EPUB_FIXTURE}")
add_executable(crosspoint_epub_zip_tests tests/epub_zip_test.cpp)
target_link_libraries(crosspoint_epub_zip_tests PRIVATE crosspoint_zip crosspoint_epub_container)
add_dependencies(crosspoint_epub_zip_tests epub_fixture)
target_compile_options(crosspoint_epub_zip_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_multilingual_epub_zip
    COMMAND crosspoint_epub_zip_tests "${EPUB_FIXTURE}")

# Tiny native/Linux diagnostic executable; not a full graphical reader.
add_executable(crosspod-epub-check tools/epub_check.cpp)
target_link_libraries(crosspod-epub-check PRIVATE crosspoint_zip crosspoint_epub_container)
add_dependencies(crosspod-epub-check epub_fixture)
add_test(NAME crosspoint_epub_check_cli
    COMMAND crosspod-epub-check "${EPUB_FIXTURE}")

# Original CrossPoint OPF manifest and spine parser. Temporary storage and
# metadata-cache callback are POSIX transitional adapters (full cache to port).
add_library(crosspoint_opf STATIC lib/Epub/parsers/ContentOpfParser.cpp)
target_include_directories(crosspoint_opf PUBLIC
    platform/posix
    lib/Epub/parsers
    lib/XmlParserUtils
    lib/Serialization
    lib/Epub
)
target_link_libraries(crosspoint_opf PUBLIC EXPAT::EXPAT)
add_executable(crosspoint_opf_spine_tests tests/opf_spine_test.cpp)
target_link_libraries(crosspoint_opf_spine_tests PRIVATE crosspoint_zip crosspoint_opf)
add_dependencies(crosspoint_opf_spine_tests epub_fixture)
target_compile_options(crosspoint_opf_spine_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_original_opf_spine
    COMMAND crosspoint_opf_spine_tests "${EPUB_FIXTURE}")

# Actual CrossPoint low-RAM on-disk metadata/spine/TOC cache and buffered IO.
add_library(crosspoint_book_cache STATIC
    lib/Epub/Epub/BookMetadataCache.cpp
)
target_include_directories(crosspoint_book_cache PUBLIC
    lib/Epub
    lib/Epub/Epub
    lib/Serialization
    lib/Utf8
    lib/ZipFile
    platform/posix
)
target_link_libraries(crosspoint_book_cache PUBLIC crosspoint_zip crosspoint_utf8)
target_link_libraries(crosspoint_opf PUBLIC crosspoint_book_cache)
