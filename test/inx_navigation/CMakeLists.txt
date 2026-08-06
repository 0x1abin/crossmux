add_executable(InxNavigationTest InxNavigationTest.cpp)

target_include_directories(InxNavigationTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(InxNavigationTest PRIVATE crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(InxNavigationTest)
