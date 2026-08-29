add_executable(MurphyM4BatchTest MurphyM4BatchTest.cpp)

target_include_directories(MurphyM4BatchTest PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/freeink-sdk/libs/hardware/BoardConfig/include
)

target_link_libraries(MurphyM4BatchTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(MurphyM4BatchTest)
