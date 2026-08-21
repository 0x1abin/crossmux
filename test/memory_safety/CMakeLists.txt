add_executable(memory_safety_tests
  MemorySafetyTest.cpp
  ${REPO_ROOT}/lib/AvatarGen/EyeShape.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/InflateReader/InflateReader.cpp
  ${REPO_ROOT}/lib/Memory/BuildScratch.cpp
  ${REPO_ROOT}/lib/uzlib/src/tinflate.c
)

target_include_directories(memory_safety_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/lib/AvatarGen
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/InflateReader
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/uzlib/src
  ${REPO_ROOT}/lib/ZipFile
)

target_link_libraries(memory_safety_tests PRIVATE GTest::gtest_main crosspoint_test_common)
gtest_discover_tests(memory_safety_tests)
