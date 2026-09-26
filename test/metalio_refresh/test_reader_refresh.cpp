#define CROSSMUX_READER_REFRESH_TEST
#include <test_ssd1677.cpp>

#include "ReaderRefresh.h"

int main() {
  testDriverSequences();
  testImageTransitionPolicy();
  int due = 0;
  assert(ReaderUtils::consumeRefreshMode(due, true) == HalDisplay::FAST_REFRESH);
  assert(due == 1);
  assert(ReaderUtils::consumeRefreshMode(due) == HalDisplay::HALF_REFRESH);
  assert(due == SETTINGS.getRefreshFrequency());
  due = 1;
  assert(ReaderUtils::consumeRefreshMode(due, true) == HalDisplay::FAST_REFRESH);
  assert(due == 1);
  // A following image can pay the same debt; alternating content cannot lose it.
  assert(ReaderUtils::consumeRefreshMode(due) == HalDisplay::HALF_REFRESH);
  due = 3;
  assert(ReaderUtils::consumeRefreshMode(due, true) == HalDisplay::FAST_REFRESH && due == 2);
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

  // Exercise the shared EPUB/TXT helper's transition and manual-refresh decisions.
  renderer.combined = true;
  renderer.transition = true;
  due = 0;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(due == SETTINGS.getRefreshFrequency() && renderer.lastMode == HalDisplay::FAST_REFRESH);
  assert(renderer.lastContext == DisplayRefreshContext::TextOnlyAntiAliasingTransition);
  renderer.transition = false;
  // The first actual page turn must not inherit artificial entry cleanup debt.
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(due == SETTINGS.getRefreshFrequency() - 1 && renderer.lastMode == HalDisplay::FAST_REFRESH);
  while (due > 1) ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(due == SETTINGS.getRefreshFrequency() && renderer.lastMode == HalDisplay::HALF_REFRESH);
  due = 0;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(renderer.lastMode == HalDisplay::HALF_REFRESH);  // Unknown source still corrects on entry.
  renderer.transition = true;
  due = 0;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, true);
  assert(renderer.lastMode == HalDisplay::HALF_REFRESH);  // Manual entry refresh is never skipped.
  due = 1;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(due == 1 && renderer.lastMode == HalDisplay::FAST_REFRESH);
  assert(renderer.lastContext == DisplayRefreshContext::TextOnlyAntiAliasingTransition);
  renderer.transition = false;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, false);
  assert(due == SETTINGS.getRefreshFrequency() && renderer.lastMode == HalDisplay::HALF_REFRESH);
  assert(renderer.lastContext == DisplayRefreshContext::TextOnlyAntiAliasing);
  renderer.transition = true;
  due = 1;
  ReaderUtils::displayBaseWithRefreshCycle(renderer, due, true);
  assert(due == SETTINGS.getRefreshFrequency() && renderer.lastMode == HalDisplay::HALF_REFRESH);
  assert(renderer.lastContext == DisplayRefreshContext::TextOnlyAntiAliasing);
}
