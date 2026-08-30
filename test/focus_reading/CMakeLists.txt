add_executable(focus_reading_tests FocusReadingRulesTest.cpp)

target_include_directories(focus_reading_tests PRIVATE ${REPO_ROOT}/lib/EpdFont ${REPO_ROOT}/lib/Epub)
target_link_libraries(focus_reading_tests PRIVATE GTest::gtest_main crosspoint_test_common)

gtest_discover_tests(focus_reading_tests)
