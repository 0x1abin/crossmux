add_executable(InxNavigationTest InxNavigationTest.cpp)

target_include_directories(InxNavigationTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(InxNavigationTest PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(InxNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(InxNavigationTest)
