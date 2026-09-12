#pragma once

class HalGPIO {
 public:
  bool deviceIsX3() const { return false; }
};

inline HalGPIO gpio;
