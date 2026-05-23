// HAL display backend for the simulator (native SDL window + WebAssembly browser builds).
//
// Strategy (both builds): keep the 48 KiB 1-bpp framebuffer on the heap, expose it via
// getFrameBuffer() so GfxRenderer writes pixels into it directly (same as on hardware). Only
// the "present" path differs per backend:
//   - native: displayBuffer() pushes the framebuffer to an SDL_Texture for the window thread
//     to unpack + present.
//   - WASM:   displayBuffer() snapshots the framebuffer into a present buffer and flips a dirty
//     flag; the browser main thread reads the snapshot via simulator_framebuffer_ptr() and
//     unpacks/rotates it onto an HTML canvas (see public/simulator/index.html in crosspoint-web).
//     No SDL, no windowing.

#include <HalDisplay.h>

#include <cstdlib>
#include <cstring>
#include <mutex>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>

#include <atomic>
#else
#include <SDL.h>

#include "../simulator_config.h"
#include "simulator_window.h"
#endif

namespace {
constexpr size_t kBufferSize = HalDisplay::BUFFER_SIZE;

std::mutex& fb_mutex() {
  static std::mutex m;
  return m;
}

uint8_t* allocate_fb() {
  auto* p = static_cast<uint8_t*>(std::malloc(kBufferSize));
  std::memset(p, 0xFF, kBufferSize);  // White by default.
  return p;
}

#ifdef __EMSCRIPTEN__
// Dirty flag set by the firmware worker thread on displayBuffer(), consumed by the browser main
// thread (via simulator_consume_dirty). std::atomic over the shared WASM heap makes the
// cross-thread hand-off well-defined.
std::atomic<int> g_dirty{1};  // start dirty so the first frame paints.

// Present buffer: a stable snapshot the browser main thread reads. displayBuffer() copies the
// live framebuffer into it under fb_mutex so the canvas never sees a frame the firmware worker
// is mid-draw on.
uint8_t* present_buffer() {
  static uint8_t* p = allocate_fb();
  return p;
}
#endif
}  // namespace

HalDisplay::HalDisplay() {}
HalDisplay::~HalDisplay() {}

void HalDisplay::begin(bool /*seamless*/) {
  // Framebuffer is allocated lazily on first getFrameBuffer() to avoid leaks when multiple
  // HalDisplay instances are constructed (shouldn't happen, but defensive). seamless is a no-op
  // in the simulator: there is no SDK resync/X3 full-sync counter.
}

uint8_t* HalDisplay::getFrameBuffer() const {
  static uint8_t* fb = allocate_fb();
  return fb;
}

void HalDisplay::clearScreen(uint8_t color) const {
  std::lock_guard<std::mutex> lock(fb_mutex());
  std::memset(getFrameBuffer(), color, kBufferSize);
}

#ifdef __EMSCRIPTEN__
void HalDisplay::displayBuffer(RefreshMode /*mode*/, bool /*turnOffScreen*/) {
  // GfxRenderer has finished writing this frame into getFrameBuffer(); snapshot it into the
  // present buffer under lock, then signal the browser side to repaint.
  std::lock_guard<std::mutex> lock(fb_mutex());
  std::memcpy(present_buffer(), getFrameBuffer(), kBufferSize);
  g_dirty.store(1, std::memory_order_release);
}
#else
void HalDisplay::displayBuffer(RefreshMode /*mode*/, bool /*turnOffScreen*/) {
  std::lock_guard<std::mutex> lock(fb_mutex());
  simulator::SimulatorWindow::instance().pushFramebuffer(getFrameBuffer());
#if SIMULATE_REFRESH_DELAY_MS > 0
  SDL_Delay(SIMULATE_REFRESH_DELAY_MS);
#endif
}
#endif

void HalDisplay::refreshDisplay(RefreshMode mode, bool turnOffScreen) { displayBuffer(mode, turnOffScreen); }

void HalDisplay::deepSleep() {}

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool /*fromProgmem*/) const {
  // Minimal blit: copy 1-bpp image rows into the framebuffer at (x, y).
  // Coordinates are in pixels; image rows must be 8-bit aligned (rowBytes = (w + 7) / 8).
  std::lock_guard<std::mutex> lock(fb_mutex());
  uint8_t* fb = getFrameBuffer();
  const uint16_t fbBytesPerRow = DISPLAY_WIDTH / 8;
  const uint16_t imgBytesPerRow = static_cast<uint16_t>((w + 7) / 8);
  for (uint16_t row = 0; row < h && (y + row) < DISPLAY_HEIGHT; row++) {
    for (uint16_t col = 0; col < w && (x + col) < DISPLAY_WIDTH; col++) {
      uint8_t bit = (imageData[row * imgBytesPerRow + (col >> 3)] >> (7 - (col & 7))) & 0x1;
      uint32_t fbIdx = (y + row) * fbBytesPerRow + ((x + col) >> 3);
      uint8_t shift = 7 - ((x + col) & 7);
      if (bit) {
        fb[fbIdx] |= (1u << shift);
      } else {
        fb[fbIdx] &= ~(1u << shift);
      }
    }
  }
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  drawImage(imageData, x, y, w, h, fromProgmem);  // Same as drawImage in simulator (no transparency).
}

// Grayscale: real device combines two gray bit-plane buffers on top of the BW frame to drive
// 4-level e-ink waveforms. The simulator has only a 1-bpp framebuffer, so all of these are
// no-ops — see displayGrayBuffer() below for the consequence.
void HalDisplay::copyGrayscaleBuffers(const uint8_t*, const uint8_t*) {}
void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t*) {}
void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t*) {}
void HalDisplay::cleanupGrayscaleBuffers(const uint8_t*) {}
void HalDisplay::displayGrayBuffer(bool /*turnOffScreen*/) {
  // Real hardware overlays the LSB/MSB gray bit-planes on the BW frame already shown by the
  // previous displayBuffer(). The simulator has only a single 1-bpp framebuffer, which at this
  // point holds the GRAYSCALE_MSB mask (cleared background + glyph edge pixels only). Pushing it
  // would replace the BW frame with hollow outlines; stay a no-op so the prior BW frame keeps
  // showing.
}

uint16_t HalDisplay::getDisplayWidth() const { return DISPLAY_WIDTH; }
uint16_t HalDisplay::getDisplayHeight() const { return DISPLAY_HEIGHT; }
uint16_t HalDisplay::getDisplayWidthBytes() const { return DISPLAY_WIDTH_BYTES; }
uint32_t HalDisplay::getBufferSize() const { return BUFFER_SIZE; }

HalDisplay display;

#ifdef __EMSCRIPTEN__
// ---------------------------------------------------------------------------
// C exports consumed by the browser side (crosspoint-web public/simulator).
// The framebuffer is 800x480 landscape, 1-bpp MSB-first; JS unpacks + rotates 90° CW to a
// 480x800 portrait canvas (see simulator_window.cpp present() for the reference algorithm that
// the JS mirrors).
// ---------------------------------------------------------------------------
extern "C" {

EMSCRIPTEN_KEEPALIVE uintptr_t simulator_framebuffer_ptr() { return reinterpret_cast<uintptr_t>(present_buffer()); }

// Returns 1 (and clears the flag) when a new frame is available since the last call.
EMSCRIPTEN_KEEPALIVE int simulator_consume_dirty() { return g_dirty.exchange(0, std::memory_order_acquire); }

EMSCRIPTEN_KEEPALIVE int simulator_display_width() { return HalDisplay::DISPLAY_WIDTH; }
EMSCRIPTEN_KEEPALIVE int simulator_display_height() { return HalDisplay::DISPLAY_HEIGHT; }
EMSCRIPTEN_KEEPALIVE int simulator_buffer_size() { return static_cast<int>(HalDisplay::BUFFER_SIZE); }

}  // extern "C"
#endif
