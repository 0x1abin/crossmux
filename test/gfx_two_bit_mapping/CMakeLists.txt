add_executable(GfxTwoBitMappingTest GfxTwoBitMappingTest.cpp)

target_include_directories(GfxTwoBitMappingTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/memory_safety/stubs
)
target_include_directories(GfxTwoBitMappingTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub/converters
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/GfxRenderer
)
target_compile_definitions(GfxTwoBitMappingTest PRIVATE FREEINK_DEVICE_EEGO_A4=1)
target_link_libraries(GfxTwoBitMappingTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(GfxTwoBitMappingTest)
