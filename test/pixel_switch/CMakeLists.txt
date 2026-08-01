add_executable(PixelSwitchStateTest
  PixelSwitchStateTest.cpp
  ${REPO_ROOT}/src/activities/apps/pixel-switch/PixelSwitchState.cpp
)

target_include_directories(PixelSwitchStateTest PRIVATE
  ${REPO_ROOT}/src/activities/apps/pixel-switch
)

target_link_libraries(PixelSwitchStateTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(PixelSwitchStateTest)
