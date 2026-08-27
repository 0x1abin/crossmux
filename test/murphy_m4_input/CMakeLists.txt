add_executable(MurphyM4InputTest MurphyM4InputTest.cpp)

target_include_directories(MurphyM4InputTest PRIVATE
  ${REPO_ROOT}/freeink-sdk/libs/hardware/InputManager/include
)

target_link_libraries(MurphyM4InputTest PRIVATE
  GTest::gtest_main
  crosspoint_test_common
)

gtest_discover_tests(MurphyM4InputTest)
