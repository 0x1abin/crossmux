add_executable(FileEditTest
  FileEditTest.cpp
  ${REPO_ROOT}/lib/FsHelpers/FsHelpers.cpp
  ${REPO_ROOT}/src/util/FileEditUtils.cpp
)

target_include_directories(FileEditTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/lib/FsHelpers
  ${REPO_ROOT}/src/util
)

target_link_libraries(FileEditTest PRIVATE crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(FileEditTest)
