#pragma once

#include <Epub/ReaderRenderSpec.h>

#include <cstdint>

// Pixel identity, separate from the last page's layout. Comparing members also
// avoids comparing padding bytes in ReaderRenderSpec.
struct ReaderPageCacheKey {
  ReaderRenderSpec spec;
  uint32_t sectionGeneration = 0;
  uint32_t renderEpoch = 0;
  int spine = -1;
  int page = -1;
  int top = 0;
  int right = 0;
  int bottom = 0;
  int left = 0;
  uint8_t orientation = 0;
  uint8_t fakeBold = 0;
  bool antiAliasing = false;
  bool inverted = false;
  bool background = false;
  bool guideLine = false;
  uint8_t guideStyle = 0;
  int8_t guideOffset = 0;

  bool operator==(const ReaderPageCacheKey&) const = default;
};

struct ReaderPageCache {
  enum class State { Empty, Ready, Skipped };
  ReaderPageCacheKey key;
  State state = State::Empty;

  bool attempted(const ReaderPageCacheKey& candidate) const { return state != State::Empty && key == candidate; }
  bool ready(const ReaderPageCacheKey& candidate) const { return state == State::Ready && key == candidate; }
};
