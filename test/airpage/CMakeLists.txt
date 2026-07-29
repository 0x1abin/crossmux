if(NOT TARGET arduino_host)
  add_subdirectory(${REPO_ROOT}/simulator/arduino-host
                   ${CMAKE_CURRENT_BINARY_DIR}/arduino-host)
endif()

add_executable(AirPageImageStoreTest
  AirPageImageStoreTest.cpp
  ${REPO_ROOT}/src/activities/apps/airpage/AirPageImageStore.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
  ${REPO_ROOT}/simulator/hal/HalStorage.cpp
)

target_include_directories(AirPageImageStoreTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/activities/apps/airpage
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/simulator/shims
)

target_compile_definitions(AirPageImageStoreTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(AirPageImageStoreTest PRIVATE
  arduino_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(AirPageImageStoreTest PROPERTIES RUN_SERIAL TRUE)
