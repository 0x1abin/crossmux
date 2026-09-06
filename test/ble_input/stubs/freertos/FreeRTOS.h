#pragma once
#include <cstdint>

using BaseType_t = int;
using UBaseType_t = unsigned;
using configSTACK_DEPTH_TYPE = uint32_t;
using TaskFunction_t = void (*)(void*);
using TaskHandle_t = void*;

#define INCLUDE_xTaskGetHandle 0
