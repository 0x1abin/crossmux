add_executable(WaveshareFunctionButtonTest WaveshareFunctionButtonTest.cpp)

target_include_directories(WaveshareFunctionButtonTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/hardware/InputManager/include
  ${REPO_ROOT}/lib/hal
)

target_link_libraries(WaveshareFunctionButtonTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WaveshareFunctionButtonTest)
