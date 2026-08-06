add_executable(BookCoverLoaderTest
  BookCoverLoaderTest.cpp
  ${REPO_ROOT}/src/util/BookCoverLoader.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(BookCoverLoaderTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/util
)

target_compile_definitions(BookCoverLoaderTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(BookCoverLoaderTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BookCoverLoaderTest PROPERTIES RUN_SERIAL TRUE)
