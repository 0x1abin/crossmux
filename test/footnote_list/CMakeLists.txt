add_executable(footnote_list_test
  FootnoteListTest.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)
target_include_directories(footnote_list_test BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
)
target_compile_definitions(footnote_list_test PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)
target_link_libraries(footnote_list_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(footnote_list_test PROPERTIES RUN_SERIAL TRUE)
