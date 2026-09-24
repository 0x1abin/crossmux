#pragma once

#include <cstdint>
#include <vector>

#include "GoStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class GoMenuActivity final : public Activity {
 public:
  GoMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~GoMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewAi, NewTwoPlayer, Stats };

  struct Item {
    ItemKind kind;
    bool disabled = false;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  GoStore::GoStats cachedStats;

  // Difficulty modal state (7-rung ArduGO ladder).
  OptionPopup difficultyPopup;

  // Resume slot info for the "Continue Game" subtitle.
  bool hasResume = false;
  GoMode resumeMode = GoMode::TwoPlayer;
  GoDifficulty resumeDifficulty = GoDifficulty::Kyu12;
  uint16_t resumeMoveCount = 0;
  uint16_t resumeElapsedSec = 0;

  void buildItems();
  void onSelect();
  void renderList();
  void renderStats();
};
