if(NOT TARGET arduino_host)
  add_subdirectory(${REPO_ROOT}/simulator/arduino-host
                   ${CMAKE_CURRENT_BINARY_DIR}/arduino-host)
endif()

add_executable(ChineseChessAITest
  ChineseChessAITest.cpp
  ${REPO_ROOT}/src/activities/apps/chinese-chess/ChineseChessAI.cpp
  ${REPO_ROOT}/src/activities/apps/chinese-chess/ChineseChessBoard.cpp
  ${REPO_ROOT}/simulator/hal/HalStorage.cpp
)

target_include_directories(ChineseChessAITest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/activities/apps/chinese-chess
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/simulator/shims
)

target_compile_definitions(ChineseChessAITest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(ChineseChessAITest PRIVATE
  arduino_host
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChineseChessAITest PROPERTIES RUN_SERIAL TRUE)
