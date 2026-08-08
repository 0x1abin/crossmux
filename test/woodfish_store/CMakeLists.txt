add_executable(WoodfishStoreTest
  WoodfishStoreTest.cpp
  ${REPO_ROOT}/src/activities/apps/woodfish/WoodfishStore.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(WoodfishStoreTest BEFORE PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src/activities/apps/woodfish
)

target_compile_definitions(WoodfishStoreTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(WoodfishStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WoodfishStoreTest PROPERTIES RUN_SERIAL TRUE)
