add_executable(BitmapMemorySourceTest
  BitmapMemorySourceTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(BitmapMemorySourceTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
)
target_include_directories(BitmapMemorySourceTest PRIVATE
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Memory
)

target_compile_definitions(BitmapMemorySourceTest PRIVATE CROSSPOINT_EMULATED=1)
target_link_libraries(BitmapMemorySourceTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(BitmapMemorySourceTest)
