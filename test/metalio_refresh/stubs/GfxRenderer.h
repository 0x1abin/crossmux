#pragma once
#include <DisplayRefreshContext.h>
struct HalDisplay {
  enum RefreshMode { FAST_REFRESH, HALF_REFRESH };
};
struct GfxRenderer {
  Ssd1677Driver& driver;
  EpdBus& bus;
  const uint8_t* fb;
  bool combinesGrayscaleBase() const { return false; }
  void displayGrayscaleBase(HalDisplay::RefreshMode mode, DisplayRefreshContext context) const {
    driver.displayGrayscaleBaseWithContext(bus, fb, mode == HalDisplay::FAST_REFRESH ? Mode::Fast : Mode::Half, false,
                                           context == DisplayRefreshContext::ContinuousReading
                                               ? RefreshContext::ContinuousReading
                                               : RefreshContext::Normal);
  }
  void displayBuffer(HalDisplay::RefreshMode mode, DisplayRefreshContext context) const {
    driver.displayWithContext(bus, fb, nullptr, mode == HalDisplay::FAST_REFRESH ? Mode::Fast : Mode::Half, false,
                              context == DisplayRefreshContext::ContinuousReading ? RefreshContext::ContinuousReading
                                                                                  : RefreshContext::Normal);
  }
  void displayBufferAsync(HalDisplay::RefreshMode mode, DisplayRefreshContext context) const {
    if (driver.displayStartWithContext(
            bus, fb, nullptr, mode == HalDisplay::FAST_REFRESH ? Mode::Fast : Mode::Half, false,
            context == DisplayRefreshContext::ContinuousReading ? RefreshContext::ContinuousReading
                                                                : RefreshContext::Normal))
      driver.displayFinish(bus, fb);
  }
};
