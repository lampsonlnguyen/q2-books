cmake_minimum_required(VERSION 3.16)
project(books_crosspoint_core LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Keep original upstream CrossPoint UTF-8 code unmodified.
add_library(crosspoint_utf8 STATIC
    lib/Utf8/Utf8.cpp
)
target_include_directories(crosspoint_utf8 PUBLIC lib/Utf8)

enable_testing()
# Deterministic standalone EPUB with one visibility paragraph per language
# and coverage for all 14 actual Books fallback script families.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_test(NAME books_multilingual_visibility_epub
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/make_language_visibility_epub.py
    ${CMAKE_CURRENT_BINARY_DIR}/Books-Multilingual-Visibility-Test.epub
)
add_executable(crosspoint_core_tests tests/utf8_test.cpp)
target_link_libraries(crosspoint_core_tests PRIVATE crosspoint_utf8)
add_test(NAME crosspoint_unicode COMMAND crosspoint_core_tests)

# SD-card reading settings/bookmarks are independent of CrossPoint engine.
# Q2 preferences must reach original CrossPoint layout/cache validation.
# Persistent Q2 bookshelf history, metadata and sort regression.
# Q2 renderer reclamation must not retain freed objects after repeated
# bookshelf-to-reader transitions or unregister callbacks of active readers.
add_executable(books_q2_memory_sink_tests tests/q2_memory_sink_lifecycle_test.cpp)
target_include_directories(books_q2_memory_sink_tests PRIVATE platform/posix)
target_compile_options(books_q2_memory_sink_tests PRIVATE -UNDEBUG)
add_test(NAME books_q2_renderer_sink_lifecycle COMMAND books_q2_memory_sink_tests)

# Nested EPUB folder enumeration and NFC-aware bookshelf search.
add_executable(books_advanced_reading_tests tests/advanced_reading_test.cpp)
target_include_directories(books_advanced_reading_tests PRIVATE
    reader lib/ZipFile lib/Epub platform/posix)
target_link_libraries(books_advanced_reading_tests PRIVATE
    crosspoint_zip EXPAT::EXPAT)
target_compile_options(books_advanced_reading_tests PRIVATE -UNDEBUG)
add_test(NAME books_advanced_reading_persistence_dictionary_footnote
    COMMAND books_advanced_reading_tests)

add_executable(books_connected_tests tests/connected_library_test.cpp)
target_include_directories(books_connected_tests PRIVATE reader)
target_link_libraries(books_connected_tests PRIVATE EXPAT::EXPAT curl)
target_compile_options(books_connected_tests PRIVATE -UNDEBUG)
add_test(NAME books_opds_kavita_and_koreader_transport_contract
    COMMAND books_connected_tests)

add_executable(books_reader_links_tests tests/reader_links_test.cpp)
target_include_directories(books_reader_links_tests PRIVATE reader)
target_compile_options(books_reader_links_tests PRIVATE -UNDEBUG)
add_test(NAME books_reader_internal_epub_href COMMAND books_reader_links_tests)

add_executable(books_shelf_search_tests tests/shelf_search_test.cpp)
target_include_directories(books_shelf_search_tests PRIVATE reader)
target_link_libraries(books_shelf_search_tests PRIVATE crosspoint_utf8)
target_compile_options(books_shelf_search_tests PRIVATE -UNDEBUG)
add_test(NAME books_shelf_recursive_search COMMAND books_shelf_search_tests)

add_executable(books_shelf_library_tests tests/shelf_library_test.cpp)
target_include_directories(books_shelf_library_tests PRIVATE reader)
target_compile_options(books_shelf_library_tests PRIVATE -UNDEBUG)
add_test(NAME books_shelf_library_history COMMAND books_shelf_library_tests)

add_executable(books_utf8_label_tests tests/utf8_label_test.cpp)
target_include_directories(books_utf8_label_tests PRIVATE reader)
target_link_libraries(books_utf8_label_tests PRIVATE crosspoint_utf8)
target_compile_options(books_utf8_label_tests PRIVATE -UNDEBUG)
add_test(NAME books_multilingual_label_clipping COMMAND books_utf8_label_tests)

add_executable(books_reading_layout_tests tests/reading_layout_test.cpp)
target_include_directories(books_reading_layout_tests PRIVATE reader lib/Epub)
target_compile_options(books_reading_layout_tests PRIVATE -UNDEBUG)
add_test(NAME books_reading_layout_preferences COMMAND books_reading_layout_tests)

add_executable(books_reader_state_tests tests/reader_state_test.cpp)
target_include_directories(books_reader_state_tests PRIVATE reader)
target_compile_options(books_reader_state_tests PRIVATE -UNDEBUG)
add_test(NAME books_reader_state_and_bookmarks COMMAND books_reader_state_tests)

add_executable(books_txt_package_tests tests/txt_package_test.cpp)
target_include_directories(books_txt_package_tests PRIVATE reader)
target_link_libraries(books_txt_package_tests PRIVATE crosspoint_txt crosspoint_zip)
target_compile_options(books_txt_package_tests PRIVATE -UNDEBUG)
add_test(NAME books_txt_epub_package COMMAND books_txt_package_tests)

# DI-01/DI-02 StarDict: plain and dictzip fixtures, synonyms, sidecar, fuzz.
set(STARDICT_FIXTURE_DIR "${CMAKE_CURRENT_BINARY_DIR}/stardict-fixture")
add_custom_command(OUTPUT "${STARDICT_FIXTURE_DIR}/plain/plain.ifo"
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_stardict_fixture.py" "${STARDICT_FIXTURE_DIR}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_stardict_fixture.py"
    VERBATIM)
add_custom_target(stardict_fixture ALL DEPENDS "${STARDICT_FIXTURE_DIR}/plain/plain.ifo")
add_executable(books_stardict_tests tests/stardict_test.cpp)
target_include_directories(books_stardict_tests PRIVATE reader)
target_link_libraries(books_stardict_tests PRIVATE ZLIB::ZLIB)
target_compile_options(books_stardict_tests PRIVATE -UNDEBUG)
add_dependencies(books_stardict_tests stardict_fixture)
add_test(NAME books_stardict_lookup_and_fuzz
    COMMAND books_stardict_tests "${STARDICT_FIXTURE_DIR}")
add_executable(books_dictionary_lookup_tests tests/dictionary_lookup_test.cpp)
target_include_directories(books_dictionary_lookup_tests PRIVATE reader)
target_link_libraries(books_dictionary_lookup_tests PRIVATE ZLIB::ZLIB)
target_compile_options(books_dictionary_lookup_tests PRIVATE -UNDEBUG)
add_dependencies(books_dictionary_lookup_tests stardict_fixture)
add_test(NAME books_dictionary_spellings_choices_vocabulary
    COMMAND books_dictionary_lookup_tests "${STARDICT_FIXTURE_DIR}")

add_executable(books_q2_theme_tests tests/q2_theme_test.cpp)
target_include_directories(books_q2_theme_tests PRIVATE reader platform/posix platform/q2)
target_compile_options(books_q2_theme_tests PRIVATE -UNDEBUG)
add_test(NAME books_q2pod_theme_colours COMMAND books_q2_theme_tests)

add_executable(books_ui_language_tests tests/ui_language_test.cpp)
target_include_directories(books_ui_language_tests PRIVATE reader)
target_compile_options(books_ui_language_tests PRIVATE -UNDEBUG)
target_compile_definitions(books_ui_language_tests PRIVATE
    BOOKS_LANG_DIR="${CMAKE_CURRENT_SOURCE_DIR}/lang")
add_test(NAME books_ui_language_follows_q2pod COMMAND books_ui_language_tests)

add_executable(books_legacy_migration_tests tests/legacy_state_migration_test.cpp)
target_include_directories(books_legacy_migration_tests PRIVATE reader)
target_compile_options(books_legacy_migration_tests PRIVATE -UNDEBUG)
add_test(NAME books_legacy_state_migration COMMAND books_legacy_migration_tests)

add_executable(books_t9_keypad_tests tests/t9_keypad_test.cpp)
target_include_directories(books_t9_keypad_tests PRIVATE reader)
target_compile_options(books_t9_keypad_tests PRIVATE -UNDEBUG)
add_test(NAME books_t9_search_keypad COMMAND books_t9_keypad_tests)

add_executable(books_xml_entity_tests tests/xml_entity_repair_test.cpp lib/Epub/htmlEntities.cpp)
target_include_directories(books_xml_entity_tests PRIVATE lib)
target_compile_options(books_xml_entity_tests PRIVATE -UNDEBUG)
add_test(NAME books_xml_entity_repair COMMAND books_xml_entity_tests)

# M5 difficult-EPUB corpus: generated here, opened and inspected by the real reader.
add_test(NAME books_m5_difficult_epub_corpus
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_m5_corpus.py"
      "${CMAKE_CURRENT_BINARY_DIR}/m5-corpus")
add_test(NAME books_m5_difficult_epub_checks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/m5_corpus_test.py"
      "$<TARGET_FILE:books-reader>" "${CMAKE_CURRENT_BINARY_DIR}/m5-corpus")
set_tests_properties(books_m5_difficult_epub_checks PROPERTIES DEPENDS books_m5_difficult_epub_corpus)

add_executable(books_xml_repair_tests tests/xml_repair_test.cpp lib/Epub/htmlEntities.cpp)
target_include_directories(books_xml_repair_tests PRIVATE lib)
target_link_libraries(books_xml_repair_tests PRIVATE EXPAT::EXPAT)
target_compile_options(books_xml_repair_tests PRIVATE -UNDEBUG)
add_test(NAME books_xml_element_repair COMMAND books_xml_repair_tests)

add_executable(books_user_guide_tests tests/user_guide_test.cpp)
target_include_directories(books_user_guide_tests PRIVATE reader)
target_link_libraries(books_user_guide_tests PRIVATE crosspoint_zip crosspoint_epub_container crosspoint_txt curl)
target_compile_options(books_user_guide_tests PRIVATE -UNDEBUG)
add_test(NAME books_user_guide_download_and_cache COMMAND books_user_guide_tests)

add_executable(books_smooth_text_tests tests/smooth_text_test.cpp)
target_include_directories(books_smooth_text_tests PRIVATE platform/q2)
target_compile_options(books_smooth_text_tests PRIVATE -UNDEBUG)
add_test(NAME books_smooth_text_levels COMMAND books_smooth_text_tests)

add_executable(books_status_bar_tests tests/status_bar_test.cpp)
target_include_directories(books_status_bar_tests PRIVATE reader)
target_compile_options(books_status_bar_tests PRIVATE -UNDEBUG)
add_test(NAME books_reading_footer_status COMMAND books_status_bar_tests)

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
find_package(JPEG REQUIRED)
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
add_executable(books-epub-check tools/epub_check.cpp)
target_link_libraries(books-epub-check PRIVATE crosspoint_zip crosspoint_epub_container)
add_dependencies(books-epub-check epub_fixture)
add_test(NAME crosspoint_epub_check_cli
    COMMAND books-epub-check "${EPUB_FIXTURE}")

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

# Standalone Linux Q2 Pod input protocol and nonblocking UNIX datagram receiver.
# No AWTK or ESP32 dependencies are needed for this interface.
add_library(books_q2_input STATIC platform/q2/Q2SocketInput.cpp)
target_include_directories(books_q2_input PUBLIC platform/q2)
find_package(Threads REQUIRED)
target_link_libraries(books_q2_input PUBLIC Threads::Threads)
add_executable(books_q2_input_tests tests/socket_input_test.cpp)
target_link_libraries(books_q2_input_tests PRIVATE books_q2_input)
target_compile_options(books_q2_input_tests PRIVATE -UNDEBUG)
add_test(NAME books_q2_input_protocol COMMAND books_q2_input_tests)

# Linux framebuffer backed by the Q2's two native portrait LCD pages.
# Static ARGB rotation tests do not require access to /dev/fb0.
add_library(books_q2_framebuffer STATIC platform/q2/Q2Framebuffer.cpp)
target_include_directories(books_q2_framebuffer PUBLIC platform/q2)
add_executable(books_framebuffer_tests tests/framebuffer_test.cpp)
target_link_libraries(books_framebuffer_tests PRIVATE books_q2_framebuffer)
target_compile_options(books_framebuffer_tests PRIVATE -UNDEBUG)
add_test(NAME books_q2_framebuffer_rotation COMMAND books_framebuffer_tests)

# Original CrossPoint bounded text arena (WordStore) and stylesheet parser.
# Both compile untouched; only their ESP32 cache reclamation and filesystem
# services are provided through POSIX compatibility interfaces.
add_library(crosspoint_layout_foundation STATIC
    lib/Epub/WordStore.cpp
    lib/Epub/css/CssParser.cpp
)
# The upstream CSS parser marks standard-algorithm comparisons constexpr.
# std::equal is constexpr in C++20, not in the older MIPS GCC C++17 library.
set_property(TARGET crosspoint_layout_foundation PROPERTY CXX_STANDARD 20)
set_property(TARGET crosspoint_layout_foundation PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(crosspoint_layout_foundation PUBLIC
    lib/Epub
    lib/Epub/css
    platform/posix
)
add_executable(crosspoint_layout_foundation_tests tests/layout_foundation_test.cpp)
target_link_libraries(crosspoint_layout_foundation_tests PRIVATE crosspoint_layout_foundation)
target_compile_options(crosspoint_layout_foundation_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_original_css_wordstore
    COMMAND crosspoint_layout_foundation_tests)

# CrossPoint's original Unicode glyph lookup, fixed-point font metrics,
# ligature substitution and font-family style selection.
add_library(crosspoint_font_metrics STATIC
    lib/EpdFont/EpdFont.cpp
    lib/EpdFont/EpdFontFamily.cpp
)
target_include_directories(crosspoint_font_metrics PUBLIC
    lib/EpdFont
    lib/Utf8
)
target_link_libraries(crosspoint_font_metrics PUBLIC crosspoint_utf8)
add_executable(crosspoint_font_metrics_tests tests/font_metrics_test.cpp)
target_link_libraries(crosspoint_font_metrics_tests PRIVATE crosspoint_font_metrics)
target_compile_options(crosspoint_font_metrics_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_unicode_font_metrics COMMAND crosspoint_font_metrics_tests)

# Original CrossPoint Unicode CJK line-breaking, punctuation and hyphenation
# primitives (used by ParsedText / TextBlock when they are ported).
add_library(crosspoint_linebreak STATIC
    lib/Epub/CjkBreaks.cpp
    lib/Epub/hyphenation/HyphenationCommon.cpp
)
target_include_directories(crosspoint_linebreak PUBLIC
    lib/Epub
    lib/Utf8
)
target_link_libraries(crosspoint_linebreak PUBLIC crosspoint_utf8)
add_executable(crosspoint_linebreak_tests tests/cjk_breaks_test.cpp)
target_link_libraries(crosspoint_linebreak_tests PRIVATE crosspoint_linebreak)
target_compile_options(crosspoint_linebreak_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_cjk_linebreak COMMAND crosspoint_linebreak_tests)


# Full original CrossPoint multilingual Liang hyphenator, including its
# generated language tries. The dictionary data is compiled into this target
# rather than replaced by an ASCII-only or Q2 native word splitter.
add_library(crosspoint_hyphenation STATIC
    lib/Epub/hyphenation/Hyphenator.cpp
    lib/Epub/hyphenation/LanguageRegistry.cpp
    lib/Epub/hyphenation/LiangHyphenation.cpp
)
# Original IndicScripts.h uses std::any_of in constexpr helpers.
# Apply C++20 to this target rather than changing u8 literal semantics in tests.
set_property(TARGET crosspoint_hyphenation PROPERTY CXX_STANDARD 20)
set_property(TARGET crosspoint_hyphenation PROPERTY CXX_STANDARD_REQUIRED ON)
target_include_directories(crosspoint_hyphenation PUBLIC
    lib
    lib/Epub
    lib/Epub/hyphenation
    lib/Utf8
)
target_link_libraries(crosspoint_hyphenation PUBLIC crosspoint_linebreak crosspoint_utf8)
add_executable(crosspoint_hyphenator_tests tests/hyphenator_test.cpp)
target_link_libraries(crosspoint_hyphenator_tests PRIVATE crosspoint_hyphenation)
target_compile_options(crosspoint_hyphenator_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_real_liang_hyphenation COMMAND crosspoint_hyphenator_tests)

# End-to-end EPUB ZIP stylesheet stream into CrossPoint's original CSS cascade.
add_executable(crosspoint_epub_css_pipeline_tests tests/epub_css_pipeline_test.cpp)
target_link_libraries(crosspoint_epub_css_pipeline_tests PRIVATE
    crosspoint_zip crosspoint_layout_foundation)
add_dependencies(crosspoint_epub_css_pipeline_tests epub_fixture)
target_compile_options(crosspoint_epub_css_pipeline_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_epub_css_pipeline
    COMMAND crosspoint_epub_css_pipeline_tests "${EPUB_FIXTURE}")

# Original CrossPoint MiniBidi (UAX#9) and Arabic contextual presentation.
# This is used by upstream ParsedText/GfxRenderer. We build unmodified C
# text direction algorithms on Linux rather than a simplified local rewrite.
add_library(crosspoint_bidi STATIC
    lib/MiniBidi/minibidi.c
    lib/MiniBidi/BidiUtils.cpp
)
target_include_directories(crosspoint_bidi PUBLIC
    lib/MiniBidi
    lib/Utf8
    platform/posix
)
target_link_libraries(crosspoint_bidi PUBLIC crosspoint_utf8)
add_executable(crosspoint_bidi_tests tests/bidi_test.cpp)
target_link_libraries(crosspoint_bidi_tests PRIVATE crosspoint_bidi)
target_compile_options(crosspoint_bidi_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_unicode_bidi_arabic COMMAND crosspoint_bidi_tests)

# Original CrossPoint ParsedText line composition/pagination engine and Page
# model. Compile-check only until the complete GfxRenderer and HTML chapter
# parser are connected. Do NOT present these object files as a working reader.
add_library(crosspoint_pagination_objects OBJECT
    lib/Epub/ParsedText.cpp
    lib/Epub/Page.cpp
    lib/Epub/blocks/TextBlock.cpp
)
# Recent upstream CrossPoint Memory.h uses C++20 constrained templates.
set_property(TARGET crosspoint_pagination_objects PROPERTY CXX_STANDARD 20)
set_property(TARGET crosspoint_pagination_objects PROPERTY CXX_STANDARD_REQUIRED ON)
target_compile_definitions(crosspoint_pagination_objects PRIVATE BOOKS_LINUX=1)
target_include_directories(crosspoint_pagination_objects PRIVATE
    lib
    lib/Epub
    lib/Epub/css
    lib/EpdFont
    lib/GfxRenderer
    lib/Utf8
    lib/MiniBidi
    lib/Serialization
    lib/XmlParserUtils
    platform/posix
)

# Q2-backed CrossPoint HalDisplay implementation. Upstream uses 1=white,
# 0=black in its byte buffer; the conversion to Q2 LCD color is explicit.
add_library(crosspoint_q2_hal_display STATIC platform/posix/HalDisplay.cpp)
target_include_directories(crosspoint_q2_hal_display PUBLIC
    platform/posix platform/q2)
target_link_libraries(crosspoint_q2_hal_display PUBLIC books_q2_framebuffer)
add_executable(crosspoint_q2_hal_display_tests tests/q2_hal_display_test.cpp)
target_link_libraries(crosspoint_q2_hal_display_tests PRIVATE crosspoint_q2_hal_display)
target_compile_options(crosspoint_q2_hal_display_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_q2_monochrome_hal COMMAND crosspoint_q2_hal_display_tests)

# Compile original upstream renderer as an object library. This is a
# compile-only integration gate: linking a real reader requires all font,
# image and FreeInk-independent Q2 adapters.
add_library(crosspoint_renderer_objects OBJECT
    lib/GfxRenderer/GfxRenderer.cpp
    lib/GfxRenderer/Bitmap.cpp
    lib/GfxRenderer/BitmapHelpers.cpp
    lib/GfxRenderer/FontCacheManager.cpp
    lib/Memory/BuildScratch.cpp
    lib/EpdFont/FontDecompressor.cpp
    lib/EpdFont/ComplexShaper.cpp
    lib/EpdFont/SdCardFont.cpp
    lib/Utf8/IndicReorder.cpp
    lib/OtShaper/OtShaper.cpp
    lib/OtShaper/OtIndic.cpp
    lib/OtShaper/OtNormalize.cpp
    lib/OtShaper/OtUse.cpp
    lib/OtShaper/OtFace.cpp
    lib/OtShaper/OtLayout.cpp
    lib/OtShaper/OtContext.cpp
    lib/OtShaper/OtPlan.cpp
    lib/OtShaper/OtPlanBuild.cpp
    lib/OtShaper/OtBuffer.cpp
    lib/OtShaper/OtGpos.cpp
    lib/OtShaper/OtGsub.cpp
    lib/OtShaper/OtTable.cpp
    platform/posix/FontAlloc.c
    platform/posix/FlashBlobCache.cpp
)
target_compile_definitions(crosspoint_renderer_objects PRIVATE BOOKS_LINUX=1)
set_target_properties(crosspoint_renderer_objects PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(crosspoint_renderer_objects PRIVATE
    lib/EpdFont/scripts
    lib/OtShaper
    lib
    lib/Epub
    lib/Epub/css
    lib/EpdFont
    lib/GfxRenderer
    lib/Utf8
    lib/MiniBidi
    lib/Serialization
    lib/Memory
    lib/XmlParserUtils
    platform/posix
    platform/q2
)

# Link gate: force every reference from the original GfxRenderer object through
# the linker. Compile-only success is not enough to establish a working engine.
add_executable(crosspoint_renderer_link_tests
    tests/renderer_link_test.cpp
    $<TARGET_OBJECTS:crosspoint_renderer_objects>)
target_include_directories(crosspoint_renderer_link_tests PRIVATE
    lib lib/Epub lib/EpdFont lib/GfxRenderer lib/Utf8 lib/MiniBidi
    lib/Memory platform/posix)
target_link_libraries(crosspoint_renderer_link_tests PRIVATE
    crosspoint_q2_hal_display crosspoint_font_metrics crosspoint_bidi
    crosspoint_utf8 crosspoint_layout_foundation crosspoint_linebreak ZLIB::ZLIB)
set_target_properties(crosspoint_renderer_link_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_compile_options(crosspoint_renderer_link_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_renderer_real_link COMMAND crosspoint_renderer_link_tests)

# Validate bounds checking for the original OpenType table reader independently
# of the still-in-progress GfxRenderer full-link integration.
add_executable(crosspoint_ot_table_tests
    tests/ot_table_test.cpp
    lib/OtShaper/OtTable.cpp)
target_include_directories(crosspoint_ot_table_tests PRIVATE lib/OtShaper)
target_compile_options(crosspoint_ot_table_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_opentype_table_bounds COMMAND crosspoint_ot_table_tests)

# Genuine upstream EPUB text-pagination and glyph-rendering integration gate.
# The small XHTML extractor in the test is NOT part of the production reader;
# complete ChapterHtmlSlimParser and CSS/images remain separately required.
add_executable(books_epub_page_preview
    tests/epub_page_preview.cpp
    lib/Epub/ParsedText.cpp
    lib/Epub/blocks/TextBlock.cpp
    $<TARGET_OBJECTS:crosspoint_renderer_objects>)
target_include_directories(books_epub_page_preview PRIVATE
    lib lib/Epub lib/Epub/blocks lib/Epub/css lib/EpdFont lib/GfxRenderer
    lib/Utf8 lib/MiniBidi lib/Memory lib/Serialization platform/posix)
target_compile_definitions(books_epub_page_preview PRIVATE BOOKS_LINUX=1)
set_target_properties(books_epub_page_preview PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(books_epub_page_preview PRIVATE
    crosspoint_zip crosspoint_hyphenation crosspoint_linebreak
    crosspoint_q2_hal_display crosspoint_font_metrics crosspoint_bidi
    crosspoint_layout_foundation crosspoint_utf8 EXPAT::EXPAT ZLIB::ZLIB)
target_compile_options(books_epub_page_preview PRIVATE -UNDEBUG)
add_dependencies(books_epub_page_preview epub_fixture)
add_test(NAME crosspoint_real_epub_text_page_preview
    COMMAND books_epub_page_preview "${EPUB_FIXTURE}" "${CMAKE_CURRENT_BINARY_DIR}/books-epub-preview.pgm")

# Separate self-contained original Vietnamese prose EPUB fixture for native
# glyph raster QA. Does not change the existing multilingual test's OPF spine.
set(VIETNAMESE_EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/vietnamese-prose.epub")
add_custom_command(OUTPUT "${VIETNAMESE_EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_vietnamese_epub_fixture.py" "${VIETNAMESE_EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_vietnamese_epub_fixture.py"
    VERBATIM)
add_custom_target(vietnamese_epub_fixture ALL DEPENDS "${VIETNAMESE_EPUB_FIXTURE}")
add_dependencies(books_epub_page_preview vietnamese_epub_fixture)
add_test(NAME crosspoint_vietnamese_book_page_preview
    COMMAND books_epub_page_preview "${VIETNAMESE_EPUB_FIXTURE}" "${CMAKE_CURRENT_BINARY_DIR}/books-vietnamese-preview.pgm" "OEBPS/chapter1.xhtml")

# Native Q2 EPUB vertical slice: original CrossPoint ZIP/OPF/spine, ParsedText,
# TextBlock and full linked GfxRenderer. The POSIX XHTML text-block collector
# remains temporary until ChapterHtmlSlimParser/CSS/images are fully ported.
add_executable(books-reader
    reader/books_reader.cpp
    lib/KOReaderSync/ChapterXPathResolver.cpp
    lib/KOReaderSync/ProgressMapper.cpp
    lib/KOReaderSync/ProgressComparison.cpp
    reader/JpegPreview.c
    platform/posix/EpubRuntime.cpp
    lib/Epub/parsers/TocNavParser.cpp
    lib/Epub/parsers/TocNcxParser.cpp
    platform/posix/ImageDecoderFactory.cpp
    lib/Epub/converters/ImageDimsProbe.cpp
    lib/Epub/htmlEntities.cpp
    lib/Epub/blocks/ImageBlock.cpp
    lib/Epub/Page.cpp
    lib/Epub/Section.cpp
    lib/Epub/parsers/ChapterHtmlSlimParser.cpp
    lib/Epub/ParsedText.cpp
    lib/Epub/blocks/TextBlock.cpp
    $<TARGET_OBJECTS:crosspoint_renderer_objects>)
# The upstream parser uses Arduino time helpers; POSIX supplies these shims.
set_source_files_properties(lib/Epub/parsers/ChapterHtmlSlimParser.cpp
    lib/Epub/Section.cpp PROPERTIES
    COMPILE_OPTIONS "-include;${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/ArduinoCompat.h")
target_include_directories(books-reader PRIVATE
    lib lib/Epub lib/Epub/Epub lib/Epub/parsers lib/Epub/blocks
    lib/Epub/css lib/EpdFont lib/GfxRenderer lib/Utf8 lib/MiniBidi
    lib/Memory lib/Serialization lib/XmlParserUtils lib/KOReaderSync
    reader platform/posix platform/q2)
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" BOOKS_VERSION LIMIT_COUNT 1)
target_compile_definitions(books-reader PRIVATE BOOKS_LINUX=1 BOOKS_VERSION="${BOOKS_VERSION}")
set_target_properties(books-reader PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(books-reader PRIVATE
    crosspoint_zip crosspoint_epub_container crosspoint_opf
    crosspoint_book_cache crosspoint_hyphenation crosspoint_linebreak
    crosspoint_q2_hal_display books_q2_input
    crosspoint_font_metrics crosspoint_bidi
    crosspoint_layout_foundation crosspoint_utf8 crosspoint_txt EXPAT::EXPAT ZLIB::ZLIB JPEG::JPEG curl)
add_dependencies(books-reader epub_fixture vietnamese_epub_fixture)
# Smooth menu text in Q2 Pod's TrueType faces (reader/UiText.hpp). Optional:
# without FreeType the menus keep the built-in bitmap font.
option(BOOKS_REQUIRE_FREETYPE "Fail without FreeType (the Q2 device build)" OFF)
if(BOOKS_REQUIRE_FREETYPE)
  find_package(Freetype REQUIRED)
else()
  find_package(Freetype QUIET)
endif()
if(FREETYPE_FOUND)
  target_compile_definitions(books-reader PRIVATE BOOKS_HAVE_FREETYPE=1)
  target_link_libraries(books-reader PRIVATE Freetype::Freetype)
  message(STATUS "Books: FreeType ${FREETYPE_VERSION_STRING}: smooth menu text on")
  add_executable(books_ui_text_tests tests/ui_text_test.cpp)
  target_include_directories(books_ui_text_tests PRIVATE reader platform/posix platform/q2)
  target_compile_definitions(books_ui_text_tests PRIVATE BOOKS_HAVE_FREETYPE=1)
  target_compile_options(books_ui_text_tests PRIVATE -UNDEBUG)
  target_link_libraries(books_ui_text_tests PRIVATE crosspoint_q2_hal_display books_q2_framebuffer Freetype::Freetype)
  add_test(NAME books_ui_text_truetype COMMAND books_ui_text_tests)
  set_tests_properties(books_ui_text_truetype PROPERTIES SKIP_RETURN_CODE 77)
else()
  message(STATUS "Books: no FreeType: menus use the built-in bitmap font")
endif()
# Opening a book is not required to index its OPF title, author and cover.
# Test with a generated EPUB3 cover-image and invalidated file identity.
add_test(NAME books_reader_unopened_shelf_index
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_shelf_indexer.py"
    "$<TARGET_FILE:books-reader>"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_shelf_index_epub.py")

set(BOOKS_LINKS_EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/books-page-links.epub")
add_custom_command(OUTPUT "${BOOKS_LINKS_EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_reader_links_epub.py"
        "${BOOKS_LINKS_EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_reader_links_epub.py"
    VERBATIM)
add_custom_target(books_links_fixture DEPENDS "${BOOKS_LINKS_EPUB_FIXTURE}")
add_dependencies(books-reader books_links_fixture)
add_test(NAME books_reader_real_page_links
    COMMAND books-reader --test-page-links "${BOOKS_LINKS_EPUB_FIXTURE}")
# Disposable localhost OPDS/Kavita mock: test Atom navigation, an actual
# streamed EPUB ZIP, KOReader partial-MD5 and XPath push/pull against a real
# crosspoint-reader process. No external server or credentials required.
add_test(NAME books_opds_paging_and_search
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/opds_paging_search_test.py"
    "$<TARGET_FILE:books-reader>")
add_test(NAME books_opds_servers_offline_and_retry
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/opds_servers_cache_test.py"
    "$<TARGET_FILE:books-reader>")
add_test(NAME books_opds_details_and_covers
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/opds_details_cover_test.py"
    "$<TARGET_FILE:books-reader>")
add_test(NAME books_koreader_sync_conflicts
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/sync_conflict_test.py"
    "$<TARGET_FILE:books-reader>" "${BOOKS_LINKS_EPUB_FIXTURE}")
add_test(NAME books_connected_network_end_to_end
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_connected_network.py"
    "$<TARGET_FILE:books-reader>" "${BOOKS_LINKS_EPUB_FIXTURE}")

# Exercise the same reader/OPDS/KOReader flows with a local TLS server.
# Trust is injected only into the host build; the Q2 binary always uses the
# firmware CA bundle and does not accept a test-CA environment override.
add_test(NAME books_connected_https_verify_and_sync
  COMMAND "${CMAKE_COMMAND}" -E env BOOKS_TEST_HTTPS=1
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_connected_network.py"
    "$<TARGET_FILE:books-reader>" "${BOOKS_LINKS_EPUB_FIXTURE}")


# Downloads across SIGTERM, a SIGKILL's leftover .part and a slow, steady link.
add_test(NAME books_opds_download_lifecycle
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/download_lifecycle_test.py"
    "$<TARGET_FILE:books-reader>" "${BOOKS_LINKS_EPUB_FIXTURE}")
add_test(NAME books_opds_download_lifecycle_https
  COMMAND "${CMAKE_COMMAND}" -E env BOOKS_TEST_HTTPS=1
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/download_lifecycle_test.py"
    "$<TARGET_FILE:books-reader>" "${BOOKS_LINKS_EPUB_FIXTURE}")

add_test(NAME books_reader_real_epub_spine
    COMMAND books-reader --validate "${EPUB_FIXTURE}")
# Active card reader must preserve CrossPoint CSS alignment, emphasis and RTL.
add_test(NAME books_reader_original_css_cascade
    COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/css_chapter_reader_test.py"
        "$<TARGET_FILE:books-reader>" "${EPUB_FIXTURE}")
add_test(NAME books_reader_vietnamese_epub
    COMMAND books-reader --render "${VIETNAMESE_EPUB_FIXTURE}"
        "${CMAKE_CURRENT_BINARY_DIR}/books-reader-vietnamese.pgm")

# Exercise real page transitions and backwards traversal without Q2 hardware.
add_test(NAME books_reader_walk_multispine
    COMMAND books-reader --walk "${EPUB_FIXTURE}")
add_test(NAME books_reader_walk_vietnamese
    COMMAND books-reader --walk "${VIETNAMESE_EPUB_FIXTURE}")

# Bounded, atomic per-book reading-position persistence (POSIX Q2 adapter).
add_executable(books_reading_position_tests tests/reading_position_test.cpp)
target_include_directories(books_reading_position_tests PRIVATE reader)
target_compile_options(books_reading_position_tests PRIVATE -UNDEBUG)
add_test(NAME books_reading_position_persistence COMMAND books_reading_position_tests)

# Font-family/size preference serialization and input validation.
add_executable(books_typography_tests tests/typography_test.cpp)
target_include_directories(books_typography_tests PRIVATE reader)
target_compile_options(books_typography_tests PRIVATE -UNDEBUG)
add_test(NAME books_two_font_preferences COMMAND books_typography_tests)

# Preserve original CrossPoint non-visible XHTML filtering semantics.
add_executable(crosspoint_visible_text_tests tests/visible_text_utils_test.cpp)
target_include_directories(crosspoint_visible_text_tests PRIVATE lib/Epub)
target_compile_options(crosspoint_visible_text_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_visible_xhtml_elements COMMAND crosspoint_visible_text_tests)

# Compile-only interface gate for the exact upstream CrossPoint XHTML parser.
# Full implementation still needs Epub/Section, images and CSS reader wiring.
add_executable(crosspoint_chapter_parser_interface_tests
    tests/original_chapter_parser_interface_test.cpp)
target_include_directories(crosspoint_chapter_parser_interface_tests PRIVATE
    lib lib/Epub lib/Epub/Epub lib/EpdFont lib/GfxRenderer
    lib/Utf8 lib/MiniBidi platform/posix)
set_target_properties(crosspoint_chapter_parser_interface_tests PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
add_test(NAME crosspoint_original_chapter_parser_interface
    COMMAND crosspoint_chapter_parser_interface_tests)

# Card app is the same CrossPoint reader ELF: q2-pod invokes it with argc==1.
# The binary gets its book path from its own scrollable /Books/ picker.
add_test(NAME books_reader_shelf_options COMMAND books-reader --test-shelf-options)
add_test(NAME books_reader_grid_view COMMAND books-reader --test-grid-shelf)
add_test(NAME books_reader_card_user_guide
    COMMAND books-reader --test-user-guide-shelf)
add_test(NAME books_reader_card_empty_library
    COMMAND books-reader --list-books "${CMAKE_CURRENT_BINARY_DIR}/empty-books")
add_test(NAME books_reader_card_list_fixture
    COMMAND books-reader --list-books "${CMAKE_CURRENT_BINARY_DIR}/books-test")

# Bounded PNG decoding with grayscale, alpha and palette input common in EPUB.
add_executable(books_png_formats_tests tests/png_formats_test.cpp)
target_include_directories(books_png_formats_tests PRIVATE reader)
target_link_libraries(books_png_formats_tests PRIVATE ZLIB::ZLIB)
target_compile_options(books_png_formats_tests PRIVATE -UNDEBUG)
add_test(NAME books_png_formats_and_crc COMMAND books_png_formats_tests)

# Alpha 2: illustrated EPUB fixture and actual original ZipFile PNG extraction.
# This is an ingestion gate, not a claim that ImageBlock paints to the LCD.
set(ILLUSTRATED_EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/illustrated.epub")
add_custom_command(OUTPUT "${ILLUSTRATED_EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_illustrated_epub_fixture.py" "${ILLUSTRATED_EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_illustrated_epub_fixture.py"
    VERBATIM)
add_custom_target(illustrated_epub_fixture ALL DEPENDS "${ILLUSTRATED_EPUB_FIXTURE}")
# Original CrossPoint EPUB 3 NAV/EPUB 2 NCX parsing and anchor mapping.
add_test(NAME books_real_nav_ncx_toc
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/toc_metadata_test.py"
      "$<TARGET_FILE:books-reader>" "${ILLUSTRATED_EPUB_FIXTURE}")


add_executable(books_illustrated_epub_resources
    tests/illustrated_epub_resource_test.cpp)
target_link_libraries(books_illustrated_epub_resources PRIVATE crosspoint_zip)
target_compile_options(books_illustrated_epub_resources PRIVATE -UNDEBUG)
add_dependencies(books_illustrated_epub_resources illustrated_epub_fixture)
add_test(NAME books_original_zip_illustrated_epub
    COMMAND books_illustrated_epub_resources "${ILLUSTRATED_EPUB_FIXTURE}")
# Verify the live XHTML collector can resolve ../styles relative to a nested
# chapter while maintaining image/text sequence. Host CI tests this with both
# GCC and Clang, not merely a separate parser stub.
add_test(NAME books_reader_relative_css_and_inline_image
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/relative_epub_resource_test.py"
      "$<TARGET_FILE:books-reader>" "${ILLUSTRATED_EPUB_FIXTURE}")

# Streaming parser regression: 3 MiB well-formed XHTML, malformed markup,
# and the 4 MiB safety limit. The active reader must process XML incrementally.
add_test(NAME books_reader_bounded_xhtml_streaming
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/xhtml_streaming_test.py"
      "$<TARGET_FILE:books-reader>" "${ILLUSTRATED_EPUB_FIXTURE}")

# End-to-end illustrated EPUB: PNG extracted, decoded, placed and rasterized
# into a 375x320 monochrome Books page.
add_dependencies(books-reader illustrated_epub_fixture)
add_test(NAME books_reader_renders_epub_png
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/check_illustrated_render.py"
      "$<TARGET_FILE:books-reader>" "${ILLUSTRATED_EPUB_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}/illustrated-page.pgm")

# Ensure real reader opens and rasterizes both PNG and JPEG inside the EPUB.
add_test(NAME books_reader_png_and_jpeg
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/epub_png_jpeg_test.py"
      "$<TARGET_FILE:books-reader>" "${ILLUSTRATED_EPUB_FIXTURE}"
      "${CMAKE_CURRENT_BINARY_DIR}/jpeg-png-page.pgm")
add_test(NAME books_reader_color_jpeg
    COMMAND books-reader --test-color-jpeg "${ILLUSTRATED_EPUB_FIXTURE}")
# These open the same illustrated EPUB, and so the same Section cache, in the
# default state folder: run them one at a time (ctest -j raced on the cache).
set_tests_properties(books_real_nav_ncx_toc books_original_zip_illustrated_epub
    books_reader_relative_css_and_inline_image books_reader_bounded_xhtml_streaming
    books_reader_renders_epub_png books_reader_png_and_jpeg books_reader_color_jpeg
    PROPERTIES RESOURCE_LOCK illustrated_epub_cache)

# Chinese/Japanese EPUB. Q2 hardware build renders this through the original
# CrossPoint Section and verifies CJK glyph fallback is actually used.
set(CJK_EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/cjk-script.epub")
add_custom_command(OUTPUT "${CJK_EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_cjk_epub_fixture.py" "${CJK_EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_cjk_epub_fixture.py"
    VERBATIM)
add_custom_target(cjk_epub_fixture ALL DEPENDS "${CJK_EPUB_FIXTURE}")
add_dependencies(books-reader cjk_epub_fixture)
add_test(NAME books_reader_cjk_original_section
    COMMAND books-reader --validate "${CJK_EPUB_FIXTURE}")

# Script fixtures: Korean/Hindi are release gates, other upstream scripts
# are parsed as real EPUBs to ensure UTF-8 survives the original Section path.
foreach(script korean devanagari bengali gujarati gurmukhi kannada malayalam
               oriya sinhala tamil telugu arabic hebrew)
    set(script_epub "${CMAKE_CURRENT_BINARY_DIR}/script-${script}.epub")
    add_custom_command(OUTPUT "${script_epub}"
      COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_multiscript_epub_fixture.py"
        "${script}" "${script_epub}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_multiscript_epub_fixture.py"
      VERBATIM)
    add_custom_target("books_fixture_${script}" ALL DEPENDS "${script_epub}")
    add_dependencies(books-reader "books_fixture_${script}")
    add_test(NAME "books_original_script_${script}"
      COMMAND books-reader --validate "${script_epub}")
endforeach()

# Large chapter regression: first page before complete pagination,
# every page reachable, reversible navigation, and peak RSS diagnostics.
set(LONG_EPUB_FIXTURE "${CMAKE_CURRENT_BINARY_DIR}/long-chapter.epub")
add_custom_command(OUTPUT "${LONG_EPUB_FIXTURE}"
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_long_epub_fixture.py"
      "${LONG_EPUB_FIXTURE}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/make_long_epub_fixture.py"
    VERBATIM)
add_custom_target(long_epub_fixture ALL DEPENDS "${LONG_EPUB_FIXTURE}")
add_dependencies(books-reader long_epub_fixture)
add_test(NAME books_reader_txt_and_go_to_percent
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/txt_progress_test.py"
      "$<TARGET_FILE:books-reader>" "${LONG_EPUB_FIXTURE}")
add_test(NAME books_reader_incremental_large_chapter
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/incremental_reader_test.py"
      "$<TARGET_FILE:books-reader>" "${LONG_EPUB_FIXTURE}")
# HTML entities in EPUB3 chapters; italic/bold in their own faces with word spaces intact
# (the italic test needs BOOKS_TEST_FONTS, the card font folder, else it reports skipped).
add_test(NAME books_reader_epub3_html_entities
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/markup_tolerance_test.py"
      "$<TARGET_FILE:books-reader>" entities)
add_test(NAME books_reader_italic_bold_faces
    COMMAND "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests/markup_tolerance_test.py"
      "$<TARGET_FILE:books-reader>" italic)
set_tests_properties(books_reader_italic_bold_faces PROPERTIES SKIP_RETURN_CODE 77)

# CrossPoint's actual XHTML chapter parser is compiled directly as a gate.
# Compile only until missing FreeInk image codecs and reading activity
# dependencies can be ported; this is not a linked reader.
add_library(crosspoint_xhtml_parser_objects OBJECT
    lib/Epub/parsers/ChapterHtmlSlimParser.cpp
)
target_compile_options(crosspoint_xhtml_parser_objects PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/ArduinoCompat.h)
set_target_properties(crosspoint_xhtml_parser_objects PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(crosspoint_xhtml_parser_objects PRIVATE
    lib
    lib/Epub
    lib/Epub/parsers
    lib/Epub/css
    lib/EpdFont
    lib/GfxRenderer
    lib/Utf8
    lib/MiniBidi
    lib/Serialization
    lib/XmlParserUtils
    platform/posix
    platform/q2
)

# CrossPoint's real section composition logic bridges XHTML parsing,
# ParsedText and Page. Compile it as an object while the remaining
# GfxRenderer/ImageBlock implementations are being linked on the renderer PR.
add_library(crosspoint_section_objects OBJECT
    lib/Epub/Section.cpp
)
target_compile_options(crosspoint_section_objects PRIVATE -include ${CMAKE_CURRENT_SOURCE_DIR}/platform/posix/ArduinoCompat.h)
set_target_properties(crosspoint_section_objects PROPERTIES
    CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_include_directories(crosspoint_section_objects PRIVATE
    lib
    lib/Epub
    lib/Epub/parsers
    lib/Epub/css
    lib/EpdFont
    lib/GfxRenderer
    lib/Utf8
    lib/MiniBidi
    lib/Serialization
    lib/XmlParserUtils
    platform/posix
)

# Experimental FULL upstream reader link gate. Unlike the compile-only gates
# above this deliberately resolves every reference used by the original
# ChapterHtmlSlimParser, Section and Page. It must pass before switching the
# Q2 user-facing reader to the original section engine.
add_executable(crosspoint_original_engine_link_tests
    tests/original_section_runtime_test.cpp
    lib/Epub/blocks/ImageBlock.cpp
    platform/posix/EpubRuntime.cpp
    lib/Epub/parsers/TocNavParser.cpp
    lib/Epub/parsers/TocNcxParser.cpp
    platform/posix/ImageDecoderFactory.cpp
    reader/JpegPreview.c
    lib/Epub/converters/ImageDimsProbe.cpp
    lib/Epub/htmlEntities.cpp
    $<TARGET_OBJECTS:crosspoint_xhtml_parser_objects>
    $<TARGET_OBJECTS:crosspoint_section_objects>
    $<TARGET_OBJECTS:crosspoint_pagination_objects>
    $<TARGET_OBJECTS:crosspoint_renderer_objects>)
target_include_directories(crosspoint_original_engine_link_tests PRIVATE
    lib lib/Epub lib/Epub/Epub lib/Epub/parsers lib/Epub/blocks
    lib/Epub/css lib/EpdFont lib/GfxRenderer lib/Utf8 lib/MiniBidi
    lib/Memory lib/Serialization lib/XmlParserUtils reader platform/posix platform/q2)
target_compile_definitions(crosspoint_original_engine_link_tests PRIVATE BOOKS_LINUX=1)
set_target_properties(crosspoint_original_engine_link_tests PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
target_link_libraries(crosspoint_original_engine_link_tests PRIVATE
    crosspoint_zip crosspoint_epub_container crosspoint_opf
    crosspoint_book_cache crosspoint_hyphenation crosspoint_linebreak
    crosspoint_q2_hal_display crosspoint_font_metrics crosspoint_bidi
    crosspoint_layout_foundation crosspoint_utf8 EXPAT::EXPAT ZLIB::ZLIB JPEG::JPEG)
add_dependencies(crosspoint_original_engine_link_tests epub_fixture illustrated_epub_fixture)
target_compile_options(crosspoint_original_engine_link_tests PRIVATE -UNDEBUG)
add_test(NAME crosspoint_original_engine_real_layout
    COMMAND crosspoint_original_engine_link_tests "${EPUB_FIXTURE}")

add_test(NAME books_smooth_glyphs_truetype
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/smooth_glyphs_test.py"
      "$<TARGET_FILE:books-reader>" "${VIETNAMESE_EPUB_FIXTURE}")
set_tests_properties(books_smooth_glyphs_truetype PROPERTIES SKIP_RETURN_CODE 77)
# Smooth text through the real reader (needs the Vietnamese fixture above).
add_test(NAME books_smooth_text_render
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/smooth_text_render_test.py"
      "$<TARGET_FILE:books-reader>" "${VIETNAMESE_EPUB_FIXTURE}")

add_executable(books_flow_renderer_tests tests/flow_renderer_test.cpp)
target_include_directories(books_flow_renderer_tests PRIVATE reader)
target_compile_options(books_flow_renderer_tests PRIVATE -UNDEBUG)
add_test(NAME books_cover_flow_renderer_cache_progress COMMAND books_flow_renderer_tests)

# Optional fonts: host fixture exercises the same streaming ZIP installer as Q2.
if(NOT CMAKE_CROSSCOMPILING)
  add_test(NAME books_font_pack_install
    COMMAND python3 "${CMAKE_CURRENT_SOURCE_DIR}/tests/font_pack_install_test.py" "$<TARGET_FILE:books-reader>")
endif()
