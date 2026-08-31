cmake_minimum_required(VERSION 3.16)
project(crosspoint_reader_tests C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.17.0
)

FetchContent_Declare(
  crosspoint_simulator
  GIT_REPOSITORY https://github.com/0x1abin/crosspoint-simulator.git
  GIT_TAG 62e48ce6d05ef360558d809eb43ecd422d0e5c29
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest crosspoint_simulator)

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
)
target_compile_options(crosspoint_test_common INTERFACE
  -Wall
  -Wextra
  -pedantic
)

enable_testing()
include(GoogleTest)

add_subdirectory(streaming_json_parser)
add_subdirectory(release_json_parser)
add_subdirectory(differential_rounding)
add_subdirectory(hyphenation_eval)
add_subdirectory(utf8_compose)
add_subdirectory(opds_filename)
add_subdirectory(html_to_plain_text)
add_subdirectory(minibidi_arabic)
add_subdirectory(combining_marks)
add_subdirectory(time_utils)
add_subdirectory(weread_webapi)
add_subdirectory(airpage)
add_subdirectory(chinese_chess)
add_subdirectory(sd_card_font_cache_format)
add_subdirectory(pixel_switch)
add_subdirectory(inx_navigation)
add_subdirectory(sloppy_digits)
add_subdirectory(woodfish_store)
add_subdirectory(book_cover_loader)
add_subdirectory(calculator)
add_subdirectory(txt_page_index)
add_subdirectory(txt_paragraph)
add_subdirectory(txt_encoding)
add_subdirectory(txt_chapter_index)
add_subdirectory(game_ui)
add_subdirectory(credential_integrity)
add_subdirectory(footnote_list)
add_subdirectory(waveshare_function_button)
add_subdirectory(gfx_two_bit_mapping)
add_subdirectory(dictionary_resource)
add_subdirectory(reading_guide_line)
add_subdirectory(png_conversion)
add_subdirectory(reading_background)
add_subdirectory(file_edit)
add_subdirectory(memory_safety)
add_subdirectory(xml_parser_utils)
add_subdirectory(token_boundary)
add_subdirectory(button_navigation)
add_subdirectory(bitmap_memory_source)
add_subdirectory(murphy_m4_batch)
add_subdirectory(murphy_m4_input)
add_subdirectory(system_settings_reset)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
