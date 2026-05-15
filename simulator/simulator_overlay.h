#pragma once

#include <SDL.h>

#include "simulator_settings.h"

namespace simulator {

// Host-side settings panel rendered on top of the eink area. F1 toggles
// visibility; left mouse clicks on known rows toggle the matching setting.
class SettingsOverlay {
 public:
  void setVisible(bool v) { visible_ = v; }
  bool isVisible() const { return visible_; }

  // Mirror the current HostSettings into the overlay so checkboxes render
  // with the right state.
  void syncFrom(const HostSettings& s) { showShell_ = s.showDeviceShell; }

  // Draw the panel. Pushes/pops its own blend state.
  void draw(SDL_Renderer* r, int winW, int winH);

  // Handle a left-click at (x, y) in window coordinates. Returns true iff
  // the click landed on the "Show device shell" row, in which case the
  // overlay has already flipped its own checkbox state and the caller
  // should persist + apply the change.
  bool handleClick(int x, int y);

 private:
  bool visible_ = false;
  bool showShell_ = true;

  // Hit-test rect for the toggle row, refreshed each draw().
  SDL_Rect shellRowRect_{0, 0, 0, 0};
};

}  // namespace simulator
