add_executable(ButtonNavigationTest
  ButtonNavigationTest.cpp
  ${REPO_ROOT}/src/util/ButtonNavigator.cpp
)

target_include_directories(ButtonNavigationTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/util
)

target_link_libraries(ButtonNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(ButtonNavigationTest)
