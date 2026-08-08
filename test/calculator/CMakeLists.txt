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
