add_executable(SdCardFontTest
  SdCardFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(SdCardFontTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(SdCardFontTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_compile_options(SdCardFontTest PRIVATE -fno-exceptions)
gtest_discover_tests(SdCardFontTest)

target_include_directories(SdCardFontTest PRIVATE ${REPO_ROOT}/lib/MiniBidi ${REPO_ROOT}/test/minibidi_arabic/stubs)
target_sources(SdCardFontTest PRIVATE ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp ${REPO_ROOT}/lib/MiniBidi/minibidi.c)

# Exercise the upstream PSRAM arena; C3 atomic replacements are covered by SdCardFontMemoryTest.
target_compile_definitions(SdCardFontTest PRIVATE BOARD_HAS_PSRAM SIMULATOR)
