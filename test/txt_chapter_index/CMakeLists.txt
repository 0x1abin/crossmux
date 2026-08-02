add_executable(txt_chapter_index_test
  TxtChapterCacheTest.cpp
  TxtChapterIndexTest.cpp
  ${REPO_ROOT}/lib/Txt/Txt.cpp
  ${REPO_ROOT}/lib/Txt/TxtChapterIndex.cpp
  ${REPO_ROOT}/lib/Txt/TxtEncoding.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)
target_include_directories(txt_chapter_index_test BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Txt
)
target_compile_definitions(txt_chapter_index_test PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
  ENABLE_CHINESE_VERSION
)
target_link_libraries(txt_chapter_index_test PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(txt_chapter_index_test PROPERTIES RUN_SERIAL TRUE)
