add_executable(SoundFeedbackTest
  SoundFeedbackTest.cpp
  ${REPO_ROOT}/lib/SoundFeedback/SoundFeedback.cpp
)

target_include_directories(SoundFeedbackTest PRIVATE
  ${REPO_ROOT}/lib/SoundFeedback
)

target_link_libraries(SoundFeedbackTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(SoundFeedbackTest)
