#pragma once
#include <stdint.h>

// HAL-owned request intent; no persistent permission crosses display calls.
enum class DisplayRefreshContext : uint8_t { Normal, ContinuousReading };
