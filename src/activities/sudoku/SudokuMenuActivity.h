#pragma once

#include <cstdint>
#include <vector>

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"
#include "SudokuBoard.h"
#include "SudokuStore.h"

class SudokuMenuActivity final : public Activity {
 public:
  SudokuMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~SudokuMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Stats };

  struct Item {
    ItemKind kind;
    SudokuBoard::Difficulty difficulty;  // valid for NewGame
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  SudokuStats cachedStats;
  uint16_t resumeElapsedSec = 0;
  SudokuBoard::Difficulty resumeDifficulty = SudokuBoard::Difficulty::Easy;
  uint8_t resumeProgressPercent = 0;

  void buildItems();
  void onSelect();
  void renderList();
  void renderStats();
};
