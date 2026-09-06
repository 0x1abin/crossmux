find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_test(NAME ReaderBuildFailureTest
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_reader_failure.py ${CMAKE_CXX_COMPILER}
)
