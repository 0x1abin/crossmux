add_executable(TimeUtilsTest
  TimeUtilsTest.cpp
  ${REPO_ROOT}/src/util/TimeUtils.cpp
)

target_include_directories(TimeUtilsTest PRIVATE
  stubs
  ${REPO_ROOT}/src/util
)

target_link_libraries(TimeUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TimeUtilsTest)
