add_executable(AirPageImageStoreTest
  AirPageImageStoreTest.cpp
  ${REPO_ROOT}/src/activities/apps/airpage/AirPageImageStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageToFramebufferDecoder.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(AirPageImageStoreTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/activities/apps/airpage
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/GfxRenderer
)

target_compile_definitions(AirPageImageStoreTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(AirPageImageStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AirPageImageStoreTest PROPERTIES RUN_SERIAL TRUE)
