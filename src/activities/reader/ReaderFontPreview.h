#pragma once

#include <cstdint>
#include <cstring>

// One menu visit, including nested font pickers. Fixed storage matches the
// persisted SD family name; no font data or dynamic strings are retained here.
class ReaderFontPreview {
 public:
  enum class Decision { Keep, Disable, Enable, Ask };

  void begin(const char* family, uint8_t pointSize, bool accelerated) {
    if (active_) return;
    std::strncpy(family_, family, sizeof(family_) - 1);
    family_[sizeof(family_) - 1] = '\0';
    pointSize_ = pointSize;
    accelerated_ = accelerated;
    active_ = true;
  }

  bool active() const { return active_; }

  Decision finish(const char* family, uint8_t pointSize, bool cached) {
    if (!active_) return Decision::Keep;
    active_ = false;
    if (family[0] == '\0') return Decision::Disable;
    if (pointSize == pointSize_ && std::strcmp(family_, family) == 0) {
      return accelerated_ ? Decision::Enable : Decision::Disable;
    }
    return cached ? Decision::Enable : Decision::Ask;
  }

 private:
  char family_[32] = {};
  uint8_t pointSize_ = 0;
  bool accelerated_ = false;
  bool active_ = false;
};
