add_executable(SloppyDigitsTest
  SloppyDigitsTest.cpp
  ${REPO_ROOT}/lib/SloppyDigits/SloppyDigitAlphabets.cpp
  ${REPO_ROOT}/lib/SloppyDigits/SloppyDigits.cpp
)

target_include_directories(SloppyDigitsTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/SloppyDigits
)

target_link_libraries(SloppyDigitsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SloppyDigitsTest)
