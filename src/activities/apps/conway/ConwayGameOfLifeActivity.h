#pragma once

#include <cstdint>

#include "../../Activity.h"

class ConwayGameOfLifeActivity final : public Activity {
 public:
  explicit ConwayGameOfLifeActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Conway", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

 private:
  static constexpr int kCellPx = 8;
  static constexpr int kSpeedSlowMs = 500;
  static constexpr int kSpeedMediumMs = 250;
  static constexpr int kSpeedFastMs = 100;
  static constexpr int kFullRefreshEvery = 50;

  int cols_ = 0;
  int rows_ = 0;
  int gridOriginX_ = 0;
  int gridOriginY_ = 0;
  int bytesPerRow_ = 0;
  uint8_t* current_ = nullptr;
  uint8_t* next_ = nullptr;

  bool running_ = false;
  uint32_t generation_ = 0;
  uint32_t lastStepMs_ = 0;
  int stepIntervalMs_ = kSpeedSlowMs;

  bool allocBuffers();
  void freeBuffers();
  void randomize();
  void step();
  void cycleSpeed();

  bool cellAt(int r, int c) const;
  void setCell(int r, int c, bool alive, uint8_t* buf);
  int countNeighbors(int r, int c) const;
};
