#pragma once

#include <cstdint>

enum class InputModality : uint8_t { Touch, Buttons };

constexpr InputModality inputModalityAfter(const InputModality current, const bool buttonActivity,
                                           const bool touchActivity) {
  if (touchActivity) return InputModality::Touch;
  if (buttonActivity) return InputModality::Buttons;
  return current;
}
