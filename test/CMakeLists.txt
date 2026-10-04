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
  GIT_TAG 58b6694dab19a626f36486a0c5f1616110deab7f
)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest crosspoint_simulator)

# Reuse the firmware's JPEGDEC pin; its CMake target requires ESP-IDF.
FetchContent_Declare(jpegdec
  GIT_REPOSITORY https://github.com/bitbank2/JPEGDEC.git
  GIT_TAG 86282979224c8a32fd51e091ed5a35b0c699a52b
)
FetchContent_GetProperties(jpegdec)
if(NOT jpegdec_POPULATED)
  FetchContent_Populate(jpegdec)
endif()

set(REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_library(crosspoint_test_common INTERFACE)
target_include_directories(crosspoint_test_common INTERFACE
  ${REPO_ROOT}
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Memory
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
add_subdirectory(library_text)
add_subdirectory(library_format)
add_subdirectory(library_index_file)
add_subdirectory(library_builder)
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
add_subdirectory(sound_feedback)
add_subdirectory(gfx_two_bit_mapping)
add_subdirectory(dictionary_resource)
add_subdirectory(reading_guide_line)
add_subdirectory(png_conversion)
add_subdirectory(sample_row_clamp)
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
add_subdirectory(user_guide)
add_subdirectory(koreader_xpath_resolver)
add_subdirectory(progress_comparison)
add_subdirectory(font_cache_manager)
add_subdirectory(css_parser)
add_subdirectory(chapter_html_slim_parser)
add_subdirectory(korean_line_breaking)
add_subdirectory(ligature_guard)
add_subdirectory(page_link)
add_subdirectory(ble_key_mapping)
add_subdirectory(ble_input)
add_subdirectory(reader_build_failure)
add_subdirectory(reader_font_preview)

# Font fallback reuses the existing Unicode mark classification.
foreach(font_test DifferentialRoundingTest LigatureGuardTest CalculatorFontTest SdCardFontMemoryTest SdCardFontPagedTest)
  target_sources(${font_test} PRIVATE
    ${REPO_ROOT}/lib/MiniBidi/minibidi.c
    ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp)
  target_include_directories(${font_test} PRIVATE
    ${REPO_ROOT}/lib/MiniBidi
    ${REPO_ROOT}/test/minibidi_arabic/stubs)
endforeach()

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME WeReadLogout
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_weread_logout.py)
add_test(NAME BuiltinFontShrink
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_builtin_font_shrink.py)
add_test(NAME PioarduinoCache
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_pioarduino_cache.py)
add_test(NAME ControlCenterGesture
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_control_center_gesture.py)
add_test(NAME ReadingUiRegressions
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_reading_ui_regressions.py)
add_test(NAME DownloadMemoryLifecycle
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_download_memory_lifecycle.py)
add_test(NAME WifiFontRecovery
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_wifi_font_recovery.py)
add_test(NAME HalClockSntp
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_hal_clock_sntp.py)
add_test(NAME InxStyleCompatibility
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_inx_style_compat.py)
add_test(NAME InxRecentThumbnails
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/check_recent_thumbnails.py ${CMAKE_CXX_COMPILER})

add_test(NAME UiFontFallback
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_ui_font_fallback.py)
add_subdirectory(content_opf_parser)
add_subdirectory(chapter_position)
add_subdirectory(home_button)
add_subdirectory(button_press_navigation)

add_subdirectory(inflate_stream)
add_subdirectory(fs_helpers)
add_subdirectory(txt_to_html)

add_subdirectory(absolute_grayscale)

add_subdirectory(sd_card_font)
add_subdirectory(glyph_bitmap)

add_test(NAME UnifiedKeyboard COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_unified_keyboard.py)
add_test(NAME TouchNetworkControls COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_touch_network.py)
add_test(NAME ThemeMenus COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_theme_menus.py)

add_test(NAME CoverGridCompatibility COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_cover_grid.py)
add_test(NAME ThemeCheckboxRows COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_checkbox_rows.py)
add_test(NAME LegacyThemeListGeometry COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_legacy_list_adapter.py)

add_test(NAME InxSettingControls COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/inx_navigation/test_inx_setting_controls.py)

add_test(NAME SettingsApiKeys COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/settings_api/test_settings_api_keys.py)

add_test(NAME HalTiltGestures
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/hal_tilt/test_hal_tilt.py)

add_test(NAME NativeGrayscale
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_native_grayscale.py
    ${crosspoint_simulator_SOURCE_DIR} ${CMAKE_CXX_COMPILER} ${jpegdec_SOURCE_DIR}/src)
set_tests_properties(NativeGrayscale PROPERTIES RUN_SERIAL TRUE)

add_test(NAME FileBrowserImages
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_file_browser_images.py)

add_test(NAME ArduinoBootstrapLto COMMAND ${Python3_EXECUTABLE} -m unittest discover -s ${REPO_ROOT}/scripts/tests -p test_pioarduino_cache.py)
add_subdirectory(reader_session)
add_subdirectory(plugin_manifest_classification)
add_subdirectory(protected_paths)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME TrustedTimeTimezone COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_trusted_time.py)

add_test(NAME WebServerStartupFailure COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_webserver_startup_failure.py)

add_test(NAME WifiStartupFailure COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_wifi_startup_failure.py)

add_test(NAME FontPreloadFlow
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/test/reader_font_preview/test_preload_flow.py)
add_test(NAME TlsMemoryPreflight COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_tls_memory_preflight.py)
add_test(NAME AirPageDisplayRetry COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_airpage_display_retry.py)

add_test(NAME FontPreloadRefresh COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_font_preload_refresh.py)

add_test(NAME ImagePsramIo COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_image_psram_io.py)

# Real device PNGdec callback semantics differ from the simulator shim.
FetchContent_Declare(pngdec
  GIT_REPOSITORY https://github.com/bitbank2/PNGdec.git
  GIT_TAG 1.1.6
)
FetchContent_GetProperties(pngdec)
if(NOT pngdec_POPULATED)
  FetchContent_Populate(pngdec)
endif()
add_test(NAME RealImageDecoderCancellation
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_real_image_decoder_cancel.py
    --png-source ${pngdec_SOURCE_DIR}/src --jpeg-source ${jpegdec_SOURCE_DIR}/src)
