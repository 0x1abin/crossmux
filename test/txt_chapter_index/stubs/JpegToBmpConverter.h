#pragma once

#include <HalStorage.h>

class JpegToBmpConverter {
 public:
  static bool jpegFileToBmpStream(HalFile&, Print&, bool = true) { return false; }
};
