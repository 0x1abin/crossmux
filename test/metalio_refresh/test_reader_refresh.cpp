#define CROSSMUX_READER_REFRESH_TEST
#include <test_ssd1677.cpp>

#include "ReaderRefresh.h"

int main() {
  testDriverSequences();
  std::array<uint8_t, 32> fb;
  fb.fill(0xA5);
  Ssd1677Driver d(ssd1677MetalioConfig());
  EpdBus b;
  d.begin(b);
  b.clear();
  GfxRenderer renderer{d, b, fb.data()};
  int pagesUntilFullRefresh = 1;
  for (int page = 0; page < 10; ++page) {
    const bool scheduledClean = pagesUntilFullRefresh <= 1;
    ReaderUtils::displayWithRefreshCycle(renderer, pagesUntilFullRefresh, page % 2 == 0);
    if (!scheduledClean)
      expect(b, {0xFC});  // AA must not force every page to FULL
    else
      expect(b, {0xFC, 0xFC});
    b.clear();
    d.displayGray(b, fb.data(), false, nullptr, false);
    d.cleanupGrayscaleBuffers(b, fb.data());
    b.clear();
  }
  d.display(b, fb.data(), nullptr, Mode::Fast, false);
  expect(b, {0xFC, 0xFC});  // A menu does not inherit reading context.
}
