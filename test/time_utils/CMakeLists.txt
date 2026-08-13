add_executable(TimeUtilsTest
  TimeUtilsTest.cpp
  ${REPO_ROOT}/src/util/TimeUtils.cpp
  ${REPO_ROOT}/src/activities/apps/standby/StandbyTime.cpp
)

target_include_directories(TimeUtilsTest PRIVATE
  stubs
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/src/activities/apps/standby
)

target_link_libraries(TimeUtilsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(TimeUtilsTest)
