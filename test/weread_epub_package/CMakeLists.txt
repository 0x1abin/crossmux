find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(PACKAGE_FUNCTIONS ${CMAKE_CURRENT_BINARY_DIR}/WeReadPackageFunctions.inc)
add_custom_command(OUTPUT ${PACKAGE_FUNCTIONS}
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/extract_package.py
    ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadClient.cpp ${PACKAGE_FUNCTIONS}
  DEPENDS extract_package.py ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadClient.cpp
)
add_executable(WeReadEpubPackageTest
  WeReadEpubPackageTest.cpp ${PACKAGE_FUNCTIONS}
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/src/StreamingJsonParser.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadProtocol.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadStore.cpp
  ${REPO_ROOT}/lib/WeReadWebApi/src/WeReadXhtmlCodec.cpp
  ${REPO_ROOT}/lib/Epub/Epub/htmlEntities.cpp
  ${REPO_ROOT}/src/util/StringUtils.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)
target_include_directories(WeReadEpubPackageTest BEFORE PRIVATE
  ${CMAKE_CURRENT_BINARY_DIR}
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/WeReadWebApi/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/freeink-sdk/libs/network/JsonSax/include
  ${REPO_ROOT}/lib/Logging
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(WeReadEpubPackageTest PRIVATE CROSSPOINT_EMULATED=1 DESTRUCTOR_CLOSES_FILE=1)
target_link_libraries(WeReadEpubPackageTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(WeReadEpubPackageTest PROPERTIES RUN_SERIAL TRUE)
add_test(NAME WeReadCacheAndSyncTest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/test_cache_and_sync.py ${CMAKE_CXX_COMPILER}
)
