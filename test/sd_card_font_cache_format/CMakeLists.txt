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
