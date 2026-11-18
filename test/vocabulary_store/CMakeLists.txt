FetchContent_GetProperties(arduinojson)
add_executable(VocabularyStoreTest VocabularyStoreTest.cpp
  ${REPO_ROOT}/src/vocabulary/VocabularyStore.cpp ${REPO_ROOT}/src/util/HtmlToPlainText.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp ${crosspoint_simulator_SOURCE_DIR}/src/HalClock.cpp)
target_include_directories(VocabularyStoreTest PRIVATE stubs ${REPO_ROOT}/src
  ${crosspoint_simulator_SOURCE_DIR}/src ${arduinojson_SOURCE_DIR}/src
  ${REPO_ROOT}/lib/Utf8 ${REPO_ROOT}/lib/Epub)
target_compile_definitions(VocabularyStoreTest PRIVATE CROSSPOINT_EMULATED=1)
target_link_libraries(VocabularyStoreTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(VocabularyStoreTest)
