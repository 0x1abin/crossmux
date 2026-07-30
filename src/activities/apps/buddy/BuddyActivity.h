#pragma once

#include <cstdint>

#include "../../Activity.h"
#include "BuddyGenerator.h"

class BuddyActivity final : public Activity {
 public:
  explicit BuddyActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("Buddy", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool handleHomeGesture() override { return true; }

 private:
  enum class Stage : uint8_t { WaitingForClaim, Crack, Burst, RevealCard, Card, Error };

  buddy::Traits traits_{};
  Stage stage_ = Stage::Error;
  uint32_t nextStageAt_ = 0;

  void advanceReveal();
  void finishClaim();
  void drawRevealFrame();
  void drawCard(bool clear = true);
  void drawError();
};
