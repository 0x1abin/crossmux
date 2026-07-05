#pragma once

#include <GfxRenderer.h>
#include <I18n.h>

#include <functional>

#include "activities/Activity.h"
#include "components/UITheme.h"
#include "util/ButtonNavigator.h"

class MappedInputManager;

/**
 * Activity for selecting UI language
 */
class LanguageSelectActivity final : public Activity {
 public:
  explicit LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("LanguageSelect", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void handleSelection();

  void onBack() { finish(); }
  ButtonNavigator buttonNavigator;
  int selectedIndex = 0;
  // Languages this build can actually render, in display-name order. Built in
  // onEnter(): hides e.g. ZH_CN on the global build (no CJK font). EN is always
  // present, so totalItems >= 1.
  uint8_t visibleIndices[getLanguageCount()] = {};
  uint8_t totalItems = 0;
};
