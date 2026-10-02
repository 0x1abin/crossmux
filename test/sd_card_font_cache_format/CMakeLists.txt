add_executable(sd_card_font_cache_format_test SdCardFontCacheFormatTest.cpp SdCardFontAlgorithmsTest.cpp)
target_link_libraries(sd_card_font_cache_format_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(sd_card_font_cache_format_test)

add_executable(SdCardFontMemoryTest
  SdCardFontMemoryTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(SdCardFontMemoryTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(SdCardFontMemoryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdCardFontMemoryTest)

add_executable(SdCardFontPagedTest
  SdCardFontMemoryTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(SdCardFontPagedTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(SdCardFontPagedTest PRIVATE CONFIG_IDF_TARGET_ESP32C3=1 FREEINK_CAP_BLE_HID_HOST=1)
target_link_libraries(SdCardFontPagedTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(SdCardFontPagedTest TEST_PREFIX "paged.")

add_executable(SdCardFontPreflightTest
  SdCardFontPreflightTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontCache.cpp
)
target_include_directories(SdCardFontPreflightTest PRIVATE
  stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/font_cache_manager/stubs
)
target_link_libraries(SdCardFontPreflightTest PRIVATE crosspoint_test_common)
target_compile_options(SdCardFontPreflightTest PRIVATE -UNDEBUG)
add_test(NAME SdCardFontPreflight COMMAND SdCardFontPreflightTest)
