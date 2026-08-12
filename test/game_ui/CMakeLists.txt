add_executable(GameUiTest
  GameUiTest.cpp
  ${REPO_ROOT}/src/activities/apps/GameUi.cpp
)
target_link_libraries(GameUiTest PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(GameUiTest BEFORE PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_include_directories(GameUiTest PRIVATE ${REPO_ROOT}/src)
gtest_discover_tests(GameUiTest)
