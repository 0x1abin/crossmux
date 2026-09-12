add_executable(CalculatorStateTest
  CalculatorStateTest.cpp
  ${REPO_ROOT}/src/activities/apps/calculator/CalculatorState.cpp
)

target_include_directories(CalculatorStateTest PRIVATE
  ${REPO_ROOT}/src/activities/apps/calculator
)

target_link_libraries(CalculatorStateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(CalculatorStateTest)

add_executable(CalculatorFontTest
  CalculatorFontTest.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFontFamily.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_include_directories(CalculatorFontTest PRIVATE
  ${REPO_ROOT}/src/activities/apps/calculator
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)
target_compile_options(CalculatorFontTest PRIVATE -UNDEBUG)
add_test(NAME CalculatorFontTest COMMAND CalculatorFontTest)
