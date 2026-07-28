#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "activities/Activity.h"

class BootActivity final : public Activity {
 public:
  enum class Mode : uint8_t {
    Splash,
    PostOta,
  };

  explicit BootActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, Mode mode = Mode::Splash,
                        bool allowAutoPreload = false)
      : Activity("Boot", renderer, mappedInput), mode_(mode), allowAutoPreload_(allowAutoPreload) {}

  void onEnter() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return mode_ == Mode::PostOta; }

 private:
  enum class Stage : uint8_t {
    Confirming,
    Copying,
    Verifying,
    Ready,
    Failed,
  };

  enum class Failure : uint8_t {
    Confirm,
    TooLarge,
    Memory,
    SdRead,
    FlashWrite,
    Verify,
  };

  void renderSplash();
  void runPostOta();

  Mode mode_;
  bool allowAutoPreload_;
  std::atomic<Stage> stage_{Stage::Confirming};
  std::atomic<Failure> failure_{Failure::Confirm};
  std::atomic<size_t> completed_{0};
  std::atomic<size_t> total_{1};
  uint8_t preloadPointSize_ = 0;
  unsigned lastRequestedPercent_ = 0;
};
