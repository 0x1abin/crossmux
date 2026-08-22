#pragma once

class HalDisplay {
 public:
  enum RefreshMode { FULL_REFRESH, HALF_REFRESH, FAST_REFRESH };

  static constexpr unsigned short DISPLAY_WIDTH = 800;
  static constexpr unsigned short DISPLAY_HEIGHT = 480;
  static constexpr unsigned short DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr unsigned int BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;
};
