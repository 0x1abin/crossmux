add_executable(WeReadProtocolTest
  WeReadProtocolTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadProtocol.cpp
)

target_include_directories(WeReadProtocolTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/WeReadWebApi/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Logging
)

target_link_libraries(WeReadProtocolTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeReadProtocolTest)

add_executable(WeReadTouchGeometryTest
  WeReadTouchGeometryTest.cpp
)

target_include_directories(WeReadTouchGeometryTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(WeReadTouchGeometryTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeReadTouchGeometryTest)

add_executable(WeReadStoreTest
  WeReadStoreTest.cpp
  ${REPO_ROOT}/lib/JsonParser/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadBrowse.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadProtocol.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadStore.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadXhtmlCodec.cpp
  ${REPO_ROOT}/lib/expat/xmlparse.c
  ${REPO_ROOT}/lib/expat/xmlrole.c
  ${REPO_ROOT}/lib/expat/xmltok.c
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(WeReadStoreTest BEFORE PRIVATE
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/WeReadWebApi/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/JsonParser
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/expat
)

target_compile_definitions(WeReadStoreTest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
  XML_CONTEXT_BYTES=1024
  XML_GE=0
)

target_link_libraries(WeReadStoreTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(WeReadStoreTest PROPERTIES RUN_SERIAL TRUE)
