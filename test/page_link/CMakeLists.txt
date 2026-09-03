add_executable(PageLinkTest PageLinkTest.cpp)
target_include_directories(PageLinkTest PRIVATE ${REPO_ROOT}/lib/Memory)
target_link_libraries(PageLinkTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(PageLinkTest)
