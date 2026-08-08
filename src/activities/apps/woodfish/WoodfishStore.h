#pragma once

#include <cstdint>

class WoodfishStore {
 public:
  static bool load(uint32_t& total);
  static bool save(uint32_t total);
};
