add_executable(footnote_list_test FootnoteListTest.cpp)
target_include_directories(footnote_list_test PRIVATE
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Memory
)
target_link_libraries(footnote_list_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(footnote_list_test)
