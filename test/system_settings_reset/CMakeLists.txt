add_executable(SystemSettingsResetTest
  SystemSettingsResetTest.cpp
  ${REPO_ROOT}/src/util/SystemSettingsReset.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(SystemSettingsResetTest BEFORE PRIVATE
  ${REPO_ROOT}/test/woodfish_store/stubs
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src/util
)

target_compile_definitions(SystemSettingsResetTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(SystemSettingsResetTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SystemSettingsResetTest PROPERTIES RUN_SERIAL TRUE)
