#pragma once

#include <cstdint>
#include <vector>

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"
#include "GomokuStore.h"

class GomokuMenuActivity final : public Activity {
 public:
  GomokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~GomokuMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Stats };

  struct Item {
    ItemKind kind;
    GomokuMode mode = GomokuMode::TwoPlayer;
    uint8_t boardSize = 15;
    bool disabled = false;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  GomokuStats cachedStats;

  // Difficulty modal state. Active when the user picked an AI option and we
  // need them to pick a level before launching the game.
  bool showingAiDifficulty = false;
  uint8_t pendingAiBoardSize = 15;
  int aiDifficultySel = static_cast<int>(GomokuAiLevel::Medium);

  // Resume slot info for the "Continue Game" subtitle.
  bool hasResume = false;
  GomokuMode resumeMode = GomokuMode::TwoPlayer;
  GomokuAiLevel resumeAiLevel = GomokuAiLevel::Medium;
  uint8_t resumeBoardSize = 15;
  uint16_t resumeMoveCount = 0;
  uint16_t resumeElapsedSec = 0;

  void buildItems();
  void onSelect();
  void renderList();
  void renderStats();
  void renderAiDifficulty();
  void handleAiDifficultyInput();
};
