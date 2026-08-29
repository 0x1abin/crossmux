#pragma once

#include <cstdint>
#include <vector>

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"
#include "ChineseChessStore.h"
#include "components/OptionPopup.h"

class ChineseChessMenuActivity final : public Activity {
 public:
  ChineseChessMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ChineseChessMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class ItemKind : uint8_t { Continue, NewGame, Stats };

  struct Item {
    ItemKind kind;
    ChineseChessMode mode = ChineseChessMode::TwoPlayer;
    bool disabled = false;
  };

  ButtonNavigator buttonNavigator;
  std::vector<Item> items;
  int selected = 0;
  bool showingStats = false;
  ChineseChessStats cachedStats;

  OptionPopup difficultyPopup;

  bool hasResume = false;
  ChineseChessMode resumeMode = ChineseChessMode::TwoPlayer;
  ChineseChessAiLevel resumeAiLevel = ChineseChessAiLevel::Medium;
  uint16_t resumeMoveCount = 0;
  uint16_t resumeElapsedSec = 0;

  void buildItems();
  void onSelect();
  void renderList();
  void renderStats();
};
