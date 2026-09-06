#include <gtest/gtest.h>

#include <algorithm>

#include "platform/BleIpcStack.h"

namespace {
struct TaskCall {
  TaskFunction_t task;
  const char* name;
  configSTACK_DEPTH_TYPE stackDepth;
  void* arg;
  UBaseType_t priority;
  TaskHandle_t* handle;
  BaseType_t core;
};
TaskCall lastCall{};
BaseType_t result = 1;
void task(void*) {}
}  // namespace

extern "C" BaseType_t __real_xTaskCreatePinnedToCore(TaskFunction_t task, const char* name,
                                                     configSTACK_DEPTH_TYPE stackDepth, void* arg, UBaseType_t priority,
                                                     TaskHandle_t* handle, BaseType_t core) {
  lastCall = {task, name, stackDepth, arg, priority, handle, core};
  return result;
}

TEST(BleIpcStack, RaisesOnlyMatchingIpcTasksAndNeverShrinks) {
  for (const BaseType_t core : {0, 1}) {
    for (const configSTACK_DEPTH_TYPE depth : {1024, 1280, 2048, 4096}) {
      __wrap_xTaskCreatePinnedToCore(task, core == 0 ? "ipc0" : "ipc1", depth, nullptr, 24, nullptr, core);
      EXPECT_EQ(lastCall.stackDepth, std::max(depth, configSTACK_DEPTH_TYPE{2048}));
    }
  }
  for (const char* name :
       {static_cast<const char*>(nullptr), "ipc", "ipc0-extra", "ipc2", "nimble_host", "ActivityManager"}) {
    __wrap_xTaskCreatePinnedToCore(task, name, 1024, nullptr, 1, nullptr, 0);
    EXPECT_EQ(lastCall.stackDepth, 1024u);
  }
  for (const BaseType_t core : {-1, 1, 2}) {
    __wrap_xTaskCreatePinnedToCore(task, "ipc0", 1024, nullptr, 24, nullptr, core);
    EXPECT_EQ(lastCall.stackDepth, 1024u);
  }
}

TEST(BleIpcStack, ForwardsArgumentsAndAllocationFailureUnchanged) {
  int arg = 0;
  TaskHandle_t handle = nullptr;
  const char* name = "ipc0";
  for (const BaseType_t expectedResult : {1, -1}) {
    result = expectedResult;
    EXPECT_EQ(__wrap_xTaskCreatePinnedToCore(task, name, 1024, &arg, 24, &handle, 0), expectedResult);
    EXPECT_EQ(lastCall.task, task);
    EXPECT_EQ(lastCall.name, name);
    EXPECT_EQ(lastCall.arg, &arg);
    EXPECT_EQ(lastCall.priority, 24u);
    EXPECT_EQ(lastCall.handle, &handle);
    EXPECT_EQ(lastCall.core, 0);
  }
}
