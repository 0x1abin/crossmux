#pragma once

#include <SDL.h>

#include <cstdint>
#include <mutex>
#include <vector>

#include "simulator_overlay.h"
#include "simulator_shell.h"

namespace simulator {

// Singleton owning the SDL window + texture used by HalDisplay_native to present frames.
// Lives in the main thread; HalDisplay calls pushFramebuffer() from whichever thread
// renders, which atomically swaps a back-buffer that the main loop picks up.
class SimulatorWindow {
 public:
  static SimulatorWindow& instance();

  // Open the SDL window. Must be called from the main thread before any pushFramebuffer().
  // `showShell` selects the initial layout (with bezel/buttons vs raw eink).
  bool open(const char* title, int scale, bool showShell);

  // Forward a 1-bpp framebuffer (48000 bytes, 800×480, MSB-first). Thread-safe.
  void pushFramebuffer(const uint8_t* bw1bpp);

  // Pump pending frame to the screen if a new one is available. Called from main loop.
  void presentIfDirty();

  // Force a redraw regardless of pending dirty state — used after toggling
  // shell visibility or showing/hiding the overlay so the screen updates
  // immediately even when the firmware isn't producing new frames.
  void forceRedraw();

  // Change shell visibility at runtime. Resizes the window and re-renders.
  void setShellVisible(bool v);

  // Access the settings overlay so the main loop can route F1/clicks to it.
  SettingsOverlay& settingsOverlay() { return overlay_; }

  // Current layout — useful for click hit-testing.
  ShellLayout layout() const { return layout_; }

  void close();

  SDL_Window* sdlWindow() const { return window_; }

 private:
  SimulatorWindow() = default;
  void present(bool force);

  SDL_Window* window_ = nullptr;
  SDL_Renderer* renderer_ = nullptr;
  SDL_Texture* texture_ = nullptr;
  ShellLayout layout_{};

  std::mutex pendingMutex_;
  std::vector<uint8_t> pending_;
  bool pendingDirty_ = false;
  // Stored decoded RGB so we can re-blit on resize/overlay toggle without a
  // new framebuffer push (otherwise the eink area would clear to black until
  // the firmware happens to redraw).
  std::vector<uint8_t> lastRgb_;

  SettingsOverlay overlay_;
};

// Forwarded by simulator_main.cpp on each SDL key event.
// buttonIndex matches HalGPIO::BTN_* constants.
void injectButton(uint8_t buttonIndex, bool down);

}  // namespace simulator
