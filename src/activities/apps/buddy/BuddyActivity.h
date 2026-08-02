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
  enum class IdleFrame : uint8_t { RestBeforeBlink, Blink, RestBeforeLift, Lift };

  buddy::Traits traits_{};
  Stage stage_ = Stage::Error;
  IdleFrame idleFrame_ = IdleFrame::RestBeforeBlink;
  uint32_t nextFrameAt_ = 0;
  bool cardRendered_ = false;

  void advanceReveal();
  void advanceIdleFrame();
  void finishClaim();
  void drawRevealFrame();
  void drawCard(bool clear = true);
  void drawError();
};
