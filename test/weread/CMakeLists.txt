add_executable(WeReadProtocolTest
  WeReadProtocolTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/src/activities/apps/weread/WeReadProtocol.cpp
)

target_include_directories(WeReadProtocolTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/apps/weread
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/simulator/shims
)

target_link_libraries(WeReadProtocolTest PRIVATE
  arduino_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeReadProtocolTest)

if(NOT TARGET arduino_host)
  add_subdirectory(${REPO_ROOT}/simulator/arduino-host
                   ${CMAKE_CURRENT_BINARY_DIR}/arduino-host)
endif()

add_executable(WeReadStoreTest
  WeReadStoreTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/src/activities/apps/weread/WeReadProtocol.cpp
  ${REPO_ROOT}/src/activities/apps/weread/WeReadStore.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${REPO_ROOT}/simulator/hal/HalStorage.cpp
)

target_include_directories(WeReadStoreTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/src/activities/apps/weread
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/simulator/shims
)

target_compile_definitions(WeReadStoreTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(WeReadStoreTest PRIVATE
  arduino_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeReadStoreTest PROPERTIES RUN_SERIAL TRUE)

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

add_executable(WeReadSimulatorShimTest
  WeReadSimulatorShimTest.cpp
  ${REPO_ROOT}/src/activities/apps/weread/WeReadHttpClient.cpp
)

target_include_directories(WeReadSimulatorShimTest PRIVATE
  ${REPO_ROOT}/src/activities/apps/weread
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/simulator/shims
)

target_compile_definitions(WeReadSimulatorShimTest PRIVATE
  CROSSPOINT_EMULATED=1
  CROSSPOINT_VERSION="test"
  CROSSPOINT_SIM_REAL_MD5=1
  CROSSPOINT_SIM_VERIFIED_TLS=1
)

target_compile_options(WeReadSimulatorShimTest PRIVATE
  -Wno-deprecated-declarations
)

target_link_libraries(WeReadSimulatorShimTest PRIVATE
  arduino_host
  crosspoint_test_common
  CURL::libcurl
  OpenSSL::Crypto
  GTest::gtest_main
)

gtest_discover_tests(WeReadSimulatorShimTest)
