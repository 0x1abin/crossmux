#pragma once

#include <cstdint>
#include <vector>

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"
#include "MinesweeperBoard.h"
#include "MinesweeperStore.h"

class MinesweeperMenuActivity final : public Activity {
 public:
  MinesweeperMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~MinesweeperMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Stats };

  struct Item {
    ItemKind kind;
    MinesweeperBoard::Difficulty difficulty;  // valid for NewGame and Continue
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  MinesweeperStats cachedStats;
  uint16_t resumeElapsedSec = 0;
  MinesweeperBoard::Difficulty resumeDifficulty = MinesweeperBoard::Difficulty::Easy;
  uint8_t resumeProgressPercent = 0;

  void buildItems();
  void onSelect();
  void renderList();
  void renderStats();
};
