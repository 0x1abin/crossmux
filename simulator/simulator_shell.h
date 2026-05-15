#pragma once

#include <SDL.h>

namespace simulator {

// Geometry of the simulator window in either shell-visible or shell-hidden
// mode. The eink area is always 480×800 (× scale) — only its offset changes.
struct ShellLayout {
  int scale;
  bool showShell;
  int windowW;
  int windowH;
  int einkX;
  int einkY;
  int einkW;
  int einkH;
};

ShellLayout computeShellLayout(bool showShell, int scale);

// Draw the black, antialiased rounded device shell: rounded body + three
// right-side button bumps (Power / Previous / Next) + four front-face buttons
// (Back / OK / Next / Previous) arranged 2+2 along the bottom bezel.
// Only call when layout.showShell is true. Does not touch the eink area —
// SimulatorWindow blits its texture there separately.
void drawShell(SDL_Renderer* r, const ShellLayout& layout);

}  // namespace simulator
