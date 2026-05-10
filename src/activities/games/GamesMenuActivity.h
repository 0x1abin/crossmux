#pragma once

#include <cstdint>
#include <vector>

#include "../../util/ButtonNavigator.h"
#include "../Activity.h"

class GamesMenuActivity final : public Activity {
 public:
  GamesMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("GamesMenu", renderer, mappedInput) {}
  ~GamesMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class GameKind : uint8_t { Sudoku };

  ButtonNavigator buttonNavigator;
  std::vector<GameKind> items;
  int selected = 0;

  void buildItems();
  void onSelect();
};
