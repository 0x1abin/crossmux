add_executable(sample_row_clamp_test SampleRowClampTest.cpp)

target_include_directories(sample_row_clamp_test PRIVATE ${REPO_ROOT}/lib/Epub/Epub/converters)

target_link_libraries(sample_row_clamp_test PRIVATE GTest::gtest_main crosspoint_test_common)

gtest_discover_tests(sample_row_clamp_test)
