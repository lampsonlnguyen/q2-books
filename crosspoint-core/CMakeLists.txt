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
