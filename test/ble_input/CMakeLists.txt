# Compile unchanged copies so quoted application includes can resolve test doubles.
configure_file(${REPO_ROOT}/src/BleInput.cpp BleInput.cpp COPYONLY)
configure_file(${REPO_ROOT}/src/MappedInputManager.cpp MappedInputManager.cpp COPYONLY)
foreach(mode IN ITEMS psram internal unavailable)
  add_executable(ble_input_${mode}_tests BleInputTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/BleInput.cpp)
  target_include_directories(ble_input_${mode}_tests PRIVATE stubs ${REPO_ROOT}/src)
  if(mode STREQUAL "psram")
    target_compile_definitions(ble_input_${mode}_tests PRIVATE FREEINK_CAP_BLE_HID_HOST=1 CROSSPOINT_BLE_HOST_PSRAM=1)
  elseif(mode STREQUAL "internal")
    target_compile_definitions(ble_input_${mode}_tests PRIVATE FREEINK_CAP_BLE_HID_HOST=1 CROSSPOINT_BLE_HOST_PSRAM=0)
  else()
    target_compile_definitions(ble_input_${mode}_tests PRIVATE FREEINK_CAP_BLE_HID_HOST=0 CROSSPOINT_BLE_HOST_PSRAM=0)
  endif()
  target_link_libraries(ble_input_${mode}_tests PRIVATE crosspoint_test_common GTest::gtest_main)
  gtest_discover_tests(ble_input_${mode}_tests TEST_PREFIX "${mode}.")
endforeach()

add_executable(ble_overlay_tests BleOverlayTest.cpp ${CMAKE_CURRENT_BINARY_DIR}/MappedInputManager.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/BleInput.cpp)
target_include_directories(ble_overlay_tests PRIVATE stubs ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include)
target_compile_definitions(ble_overlay_tests PRIVATE FREEINK_CAP_BLE_HID_HOST=1 CROSSPOINT_BLE_HOST_PSRAM=0)
target_link_libraries(ble_overlay_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ble_overlay_tests)

foreach(activity IN ITEMS BluetoothSettingsActivity BleButtonMapActivity)
  configure_file(${REPO_ROOT}/src/activities/settings/${activity}.cpp ${activity}.cpp COPYONLY)
  configure_file(${REPO_ROOT}/src/activities/settings/${activity}.h ${activity}.h COPYONLY)
endforeach()
target_sources(ble_overlay_tests PRIVATE BleSettingsTest.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/BluetoothSettingsActivity.cpp ${CMAKE_CURRENT_BINARY_DIR}/BleButtonMapActivity.cpp)
target_include_directories(ble_overlay_tests PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_executable(ble_ipc_stack_tests BleIpcStackTest.cpp)
target_include_directories(ble_ipc_stack_tests PRIVATE stubs ${REPO_ROOT}/src)
target_compile_definitions(ble_ipc_stack_tests PRIVATE CONFIG_IDF_TARGET_ESP32S3=1 FREEINK_CAP_BLE_HID_HOST=1)
target_link_libraries(ble_ipc_stack_tests PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ble_ipc_stack_tests)

add_test(NAME NimblePsramMiddleware
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_configure_nimble_psram.py)
add_test(NAME BleHostCompat
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_ble_host_compat.py)
add_test(NAME BleHostStartup
  COMMAND ${Python3_EXECUTABLE}
    ${REPO_ROOT}/freeink-sdk/libs/network/BleKeyboardHost/test/host/test_startup.py)
add_test(NAME BleC3Config
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_ble_c3_config.py)
add_test(NAME BleChapterLifecycle
  COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/tests/test_ble_chapter_lifecycle.py)
