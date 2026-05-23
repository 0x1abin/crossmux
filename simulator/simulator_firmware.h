#pragma once
// Shared firmware-task launcher for both simulator entry points (native SDL + WASM browser).
// The firmware's Arduino setup()/loop() are renamed to firmware_setup()/firmware_loop() via the
// CMake -Dsetup=firmware_setup -Dloop=firmware_loop flags, so the simulator can drive them.

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Provided by src/main.cpp (renamed via build flags).
void firmware_setup();
void firmware_loop();

namespace simulator {

inline void firmwareTask(void* /*unused*/) {
  firmware_setup();
  while (true) {
    firmware_loop();
  }
}

// Run firmware setup()+loop() on a registered FreeRTOS task so xTaskGetCurrentTaskHandle()
// returns a non-null handle inside firmware code — ActivityManager's RenderLock-holder
// assertion relies on that. On native the main thread keeps owning the SDL window/event loop;
// on Emscripten this task maps to a Web Worker (free to block on notifications/mutexes).
inline void startFirmwareTask() { xTaskCreate(&firmwareTask, "firmware", 8192, nullptr, 1, nullptr); }

}  // namespace simulator
