add_executable(txt_encoding_test
  TxtEncodingTest.cpp
  ${REPO_ROOT}/lib/Txt/TxtEncoding.cpp
)
target_compile_definitions(txt_encoding_test PRIVATE ENABLE_CHINESE_VERSION)
target_include_directories(txt_encoding_test PRIVATE ${REPO_ROOT}/lib/Txt)
target_link_libraries(txt_encoding_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(txt_encoding_test)
