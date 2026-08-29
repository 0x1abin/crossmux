#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace MurphyM4BatchDetection {

constexpr std::size_t SAMPLE_COUNT = 7;
constexpr uint32_t FIRST_BATCH_RISE_MIN_US = 5200;
constexpr uint32_t FIRST_BATCH_RISE_MAX_US = 10000;
constexpr uint32_t FIRST_BATCH_RATIO_MIN_PERCENT = 75;
constexpr uint32_t FIRST_BATCH_RATIO_MAX_PERCENT = 125;

constexpr uint32_t median(std::array<uint32_t, SAMPLE_COUNT>& samples) {
  for (std::size_t i = 1; i < samples.size(); ++i) {
    const uint32_t value = samples[i];
    std::size_t j = i;
    while (j > 0 && samples[j - 1] > value) {
      samples[j] = samples[j - 1];
      --j;
    }
    samples[j] = value;
  }
  return samples[samples.size() / 2];
}

constexpr bool isFirstBatch(const uint32_t referenceRiseUs, const uint32_t targetRiseUs) {
  if (referenceRiseUs < FIRST_BATCH_RISE_MIN_US || referenceRiseUs > FIRST_BATCH_RISE_MAX_US ||
      targetRiseUs < FIRST_BATCH_RISE_MIN_US || targetRiseUs > FIRST_BATCH_RISE_MAX_US) {
    return false;
  }

  const uint64_t scaledTarget = static_cast<uint64_t>(targetRiseUs) * 100;
  return scaledTarget >= static_cast<uint64_t>(referenceRiseUs) * FIRST_BATCH_RATIO_MIN_PERCENT &&
         scaledTarget <= static_cast<uint64_t>(referenceRiseUs) * FIRST_BATCH_RATIO_MAX_PERCENT;
}

}  // namespace MurphyM4BatchDetection
