#pragma once

#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class GfxRenderer;

// Full-screen frontlight adjustment page showing both the brightness and warmth
// sliders on one screen. Dragging a bar (or pressing the front/side buttons)
// updates the value live; leaving via Back/Confirm writes both values to
// CrossPointSettings and saves them.
class FrontlightAdjustmentActivity final : public Activity {
 public:
  explicit FrontlightAdjustmentActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                        const char* activityName, StrId titleId)
      : Activity(activityName, renderer, mappedInput), titleId(titleId) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  StrId titleId;
  ButtonNavigator buttonNavigator;
  bool draggingBrightness = false;
  bool draggingWarmth = false;

  void adjustBrightness(int delta);
  void adjustWarmth(int delta);
  void saveAndFinish();
};
