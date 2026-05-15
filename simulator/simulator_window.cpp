#include "simulator_window.h"

#include <HalDisplay.h>

#include <cstdio>
#include <cstring>

namespace simulator {

SimulatorWindow& SimulatorWindow::instance() {
  static SimulatorWindow w;
  return w;
}

bool SimulatorWindow::open(const char* title, int scale, bool showShell) {
  if (window_) return true;
  if (scale <= 0) scale = 1;
  layout_ = computeShellLayout(showShell, scale);
  if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
    std::fprintf(stderr, "SDL_InitSubSystem failed: %s\n", SDL_GetError());
    return false;
  }
  // The framebuffer is logically 800×480 landscape (matches hardware), but the X4
  // device is held in portrait orientation. The window presents portrait, and the
  // shell (if enabled) adds bezel margins around the eink area.
  window_ = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                             layout_.windowW, layout_.windowH, SDL_WINDOW_SHOWN);
  if (!window_) {
    std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
    return false;
  }
  renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!renderer_) {
    std::fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError());
    return false;
  }
  // Texture stored already rotated — width/height swapped — so it can be
  // stretched directly into the (sub-)rectangle within the window.
  texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING,
                               HalDisplay::DISPLAY_HEIGHT, HalDisplay::DISPLAY_WIDTH);
  if (!texture_) {
    std::fprintf(stderr, "SDL_CreateTexture failed: %s\n", SDL_GetError());
    return false;
  }
  pending_.resize(HalDisplay::BUFFER_SIZE);
  // Initial paint: all white.
  std::memset(pending_.data(), 0xFF, pending_.size());
  pendingDirty_ = true;
  present(true);
  return true;
}

void SimulatorWindow::pushFramebuffer(const uint8_t* bw1bpp) {
  if (!bw1bpp || pending_.empty()) return;
  std::lock_guard<std::mutex> lock(pendingMutex_);
  std::memcpy(pending_.data(), bw1bpp, pending_.size());
  pendingDirty_ = true;
}

void SimulatorWindow::presentIfDirty() {
  present(false);
}

void SimulatorWindow::forceRedraw() {
  present(true);
}

void SimulatorWindow::setShellVisible(bool v) {
  if (!window_) return;
  if (v == layout_.showShell) return;
  layout_ = computeShellLayout(v, layout_.scale);
  SDL_SetWindowSize(window_, layout_.windowW, layout_.windowH);
  SDL_SetWindowPosition(window_, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
  present(true);
}

void SimulatorWindow::present(bool force) {
  if (!texture_ || !renderer_) return;

  bool haveNewFrame = false;
  std::vector<uint8_t> local;
  {
    std::lock_guard<std::mutex> lock(pendingMutex_);
    if (pendingDirty_) {
      local = pending_;
      pendingDirty_ = false;
      haveNewFrame = true;
    }
  }

  // Unpack 1-bpp MSB-first → RGB24, rotating 90° clockwise so the X4's natural
  // portrait orientation is what the simulator window shows.
  //
  // CW 90° mapping: source pixel (x, y) on the 800×480 framebuffer goes to
  // destination (dst_x, dst_y) = (SRC_H - 1 - y, x) on a 480×800 RGB buffer.
  // bit=0 means black (ink on), bit=1 means white.
  constexpr uint16_t SRC_W = HalDisplay::DISPLAY_WIDTH;
  constexpr uint16_t SRC_H = HalDisplay::DISPLAY_HEIGHT;
  constexpr uint16_t DST_W = SRC_H;  // 480
  constexpr uint16_t DST_H = SRC_W;  // 800
  constexpr size_t DST_BYTES = static_cast<size_t>(DST_W) * DST_H * 3;

  if (haveNewFrame) {
    if (lastRgb_.size() != DST_BYTES) lastRgb_.resize(DST_BYTES);
    const uint16_t widthBytes = SRC_W / 8;
    for (uint16_t y = 0; y < SRC_H; y++) {
      for (uint16_t x = 0; x < SRC_W; x++) {
        uint8_t byte = local[y * widthBytes + (x >> 3)];
        bool white = (byte >> (7 - (x & 7))) & 0x1;
        uint8_t v = white ? 0xF0 : 0x10;
        const uint16_t dst_x = SRC_H - 1 - y;
        const uint16_t dst_y = x;
        size_t idx = (static_cast<size_t>(dst_y) * DST_W + dst_x) * 3;
        lastRgb_[idx + 0] = v;
        lastRgb_[idx + 1] = v;
        lastRgb_[idx + 2] = v;
      }
    }
    SDL_UpdateTexture(texture_, nullptr, lastRgb_.data(), DST_W * 3);
  } else if (!force) {
    return;  // Nothing new and no caller-forced redraw — skip.
  }

  // Clear behind the eink area. With shell on, the shell draw fills the rest;
  // with shell off, this clears the whole window to a neutral grey.
  SDL_SetRenderDrawColor(renderer_, 0x20, 0x20, 0x20, 0xFF);
  SDL_RenderClear(renderer_);

  if (layout_.showShell) {
    drawShell(renderer_, layout_);
  }

  SDL_Rect dst{layout_.einkX, layout_.einkY, layout_.einkW, layout_.einkH};
  SDL_RenderCopy(renderer_, texture_, nullptr, &dst);

  if (overlay_.isVisible()) {
    overlay_.draw(renderer_, layout_.windowW, layout_.windowH);
  }

  SDL_RenderPresent(renderer_);
}

void SimulatorWindow::close() {
  if (texture_) SDL_DestroyTexture(texture_);
  if (renderer_) SDL_DestroyRenderer(renderer_);
  if (window_) SDL_DestroyWindow(window_);
  texture_ = nullptr;
  renderer_ = nullptr;
  window_ = nullptr;
}

}  // namespace simulator
