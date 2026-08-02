add_executable(ChineseChessAITest
  ChineseChessAITest.cpp
  ${REPO_ROOT}/src/activities/apps/chinese-chess/ChineseChessAI.cpp
  ${REPO_ROOT}/src/activities/apps/chinese-chess/ChineseChessBoard.cpp
  ${crosspoint_simulator_SOURCE_DIR}/src/HalStorage.cpp
)

target_include_directories(ChineseChessAITest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${crosspoint_simulator_SOURCE_DIR}/src
  ${REPO_ROOT}/src/activities/apps/chinese-chess
  ${REPO_ROOT}/lib/hal
)

target_compile_definitions(ChineseChessAITest PRIVATE
  CROSSPOINT_EMULATED=1
  DESTRUCTOR_CLOSES_FILE=1
)

target_link_libraries(ChineseChessAITest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(ChineseChessAITest PROPERTIES RUN_SERIAL TRUE)
