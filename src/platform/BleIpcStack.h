#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#if CONFIG_IDF_TARGET_ESP32S3 && FREEINK_CAP_BLE_HID_HOST
extern "C" BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t task, const char* name,
                                                     configSTACK_DEPTH_TYPE stackDepth, void* arg, UBaseType_t priority,
                                                     TaskHandle_t* handle, BaseType_t core);

// The pinned Arduino core still gives IPC tasks 1 KiB; controller interrupt
// allocation can overflow it. Match upstream lib-builder #386's 2 KiB budget.
// ponytail: remove this adapter once the pinned core provides that budget.
// Both existing IPC stacks together require at most 2 KiB more internal RAM.
extern "C" BaseType_t __wrap_xTaskCreatePinnedToCore(TaskFunction_t task, const char* name,
                                                     configSTACK_DEPTH_TYPE stackDepth, void* arg, UBaseType_t priority,
                                                     TaskHandle_t* handle, BaseType_t core) {
  constexpr configSTACK_DEPTH_TYPE kMinIpcStackBytes = 2048;
  if (name && stackDepth < kMinIpcStackBytes &&
      ((core == 0 && std::strcmp(name, "ipc0") == 0) || (core == 1 && std::strcmp(name, "ipc1") == 0))) {
    stackDepth = kMinIpcStackBytes;
  }
  return __real_xTaskCreatePinnedToCore(task, name, stackDepth, arg, priority, handle, core);
}
#endif
