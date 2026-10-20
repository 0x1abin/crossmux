# Reuse the firmware's exact ArduinoJson version for allocator-failure tests.
FetchContent_Declare(arduinojson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2
)
FetchContent_GetProperties(arduinojson)
if(NOT arduinojson_POPULATED)
  FetchContent_Populate(arduinojson)
endif()

add_test(NAME PersistableStoreWrite
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/check_write.py
    ${CMAKE_CXX_COMPILER} ${arduinojson_SOURCE_DIR}/src ${crosspoint_simulator_SOURCE_DIR}/src)
