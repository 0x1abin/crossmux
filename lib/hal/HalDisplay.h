#pragma once
#include <Arduino.h>
#include <BoardConfig.h>
#include <DisplayRefreshContext.h>
#include <EInkDisplay.h>

class HalDisplay {
 public:
  // Constructor with pin configuration
  HalDisplay();

  // Destructor
  ~HalDisplay();

  // Refresh modes
  enum RefreshMode {
    FULL_REFRESH,  // Full refresh with complete waveform
    HALF_REFRESH,  // Half refresh (1720ms) - balanced quality and speed
    FAST_REFRESH   // Fast refresh using custom LUT
  };

  // Pass seamless=true on any path where the panel already shows the
  // content it should after begin() returns (silent reboot's popup,
  // sleep-wake with a restored buffer). Skips the wakeup-gated
  // requestResync() and defuses the SDK's X3 _x3InitialFullSyncsRemaining
  // counter; otherwise the first two paints get promoted to FULL
  // (~770ms each on X3).
  void begin(bool seamless = false);

  // Compile-time geometry, in the panel's NATIVE scan frame (source columns x
  // gate lines) — the same frame the SDK facade reports at runtime, before any
  // orientation rotation. These are the initializers GfxRenderer's members start
  // from; GfxRenderer::begin() replaces them with the live values from
  // display.getDisplayWidth()/getDisplayHeight()/getDisplayWidthBytes()/
  // getBufferSize() (GfxRenderer.cpp:134-137), which is the single source of
  // truth for layout. Nothing in CrossMux may hardcode 800/480 instead of asking
  // the renderer (golden rule #8).
  //
  // Every target except Read Pico keeps the SDK's 800x480 / 48,000-byte default
  // verbatim, including the X3 whose 792x528 panel is a runtime override on top
  // of it. Read Pico's panel is 1216x684 with a 103,968-byte 1-bpp framebuffer
  // (read-pico.md 1.2), so it cannot ride the 800x480 default: a compile-time
  // consumer sized from it would be 2.2x too small. The device term comes from
  // the frozen profile, not from a repeated literal.
#if FREEINK_DEVICE_READPICO
  static constexpr uint16_t DISPLAY_WIDTH = BoardConfig::READ_PICO.displayWidth;
  static constexpr uint16_t DISPLAY_HEIGHT = BoardConfig::READ_PICO.displayHeight;
#else
  static constexpr uint16_t DISPLAY_WIDTH = EInkDisplay::DISPLAY_WIDTH;
  static constexpr uint16_t DISPLAY_HEIGHT = EInkDisplay::DISPLAY_HEIGHT;
#endif
  static constexpr uint16_t DISPLAY_WIDTH_BYTES = DISPLAY_WIDTH / 8;
  static constexpr uint32_t BUFFER_SIZE = DISPLAY_WIDTH_BYTES * DISPLAY_HEIGHT;

  // Frame buffer operations
  void clearScreen(uint8_t color = 0xFF) const;
  void drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                 bool fromProgmem = false) const;
  void drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                            bool fromProgmem = false) const;

  void displayBuffer(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false,
                     DisplayRefreshContext context = DisplayRefreshContext::Normal);
  // Non-blocking refresh (shadow-free): starts the panel waveform and returns
  // while the panel refreshes on its own. The framebuffer must stay untouched
  // until waitRefreshComplete(), and the caller must rebuild the differential
  // baseline before the next differential update (the tiled grayscale cleanup
  // does). Panels without deferral fall back to a blocking refresh.
  void displayBufferAsync(RefreshMode mode = RefreshMode::FAST_REFRESH,
                          DisplayRefreshContext context = DisplayRefreshContext::Normal);
  // Block until a pending deferred refresh completes (no-op when none is).
  void waitRefreshComplete();
  // True when displayBufferAsync() genuinely overlaps (panel driver defers);
  // false where it falls back to a blocking refresh.
  bool supportsAsyncRefresh() const;
  void refreshDisplay(RefreshMode mode = RefreshMode::FAST_REFRESH, bool turnOffScreen = false);

  // Output polarity. The framebuffer remains in normal polarity; inversion is
  // applied by the display driver while sending it to the panel.
  void setInverted(bool inverted);
  bool toggleInverted();
  bool isInverted() const;

  // Power management
  void deepSleep();

  // Access to frame buffer
  uint8_t* getFrameBuffer() const;

  // Lend the framebuffer's ~48 KB STORAGE to a memory-hungry phase (chapter
  // builds) without freeing it: the allocation never moves, so repeated loans
  // cannot fragment the heap (free+realloc measurably did). No display calls
  // between lend and return; the panel keeps its last refreshed image. The
  // buffer comes back white — redraw fully. Returns nullptr if already lent.
  uint8_t* lendFrameBufferStorage(uint32_t* sizeOut);
  void returnFrameBufferStorage();

  // X3 grayscale preconditioning (OEM "AA-pre-BW(mid)" settle pass), windowed
  // to the gray region in physical panel coordinates (no-arg = full frame).
  // Call after the BW base frame is displayed and before the grayscale planes
  // are written; no-op on X4. See EInkDisplay::preconditionGrayscale.
  void preconditionGrayscale();
  void preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h);

  // Display the framebuffer as the base frame for a grayscale overlay that
  // follows. On X3, HALF fallback first requests a resync to match
  // displayBuffer(HALF); FAST fallback keeps the OEM differential base waveform
  // ("AA-pre-BW(mid)"). Other panels display normally with `fallback` mode.
  void displayGrayscaleBase(RefreshMode fallback = HALF_REFRESH, bool turnOffScreen = false,
                            DisplayRefreshContext context = DisplayRefreshContext::Normal);

  void copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer);
  void copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer);
  void copyGrayscaleMsbBuffers(const uint8_t* msbBuffer);
  void cleanupGrayscaleBuffers(const uint8_t* bwBuffer);

  void displayGrayBuffer(bool turnOffScreen = false);

  // Tiled grayscale: stream one band of a plane (lsbPlane selects LSB/MSB RAM)
  // straight to the controller; supportsStripGrayscale() gates the path. See
  // EInkDisplay::writeGrayscalePlaneStrip.
  void writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows);
  bool supportsStripGrayscale() const;

  // True when displayGrayscaleBase() defers the base activation so the gray
  // planes join it in a single waveform (Paper Mono). Callers should then route
  // the base of a grayscale page through displayGrayscaleBase() instead of
  // displayBuffer(): a separate B/W refresh first makes the gray pass re-drive
  // the whole text body (a visible flash).
  bool combinesGrayscaleBase() const;
  bool supportsTextOnlyCombinedBase() const;
  bool supportsReaderTransitions() const;
  bool supportsContinuousImageReading() const;
  bool canUseTextTransition() const;
  void cancelGrayscale();

  // Runtime geometry passthrough — the authoritative source for every layout
  // decision (GfxRenderer reads exactly these in its begin()). Prefer them over
  // the compile-time constants above, which only describe the panel before the
  // driver reports its real geometry.
  uint16_t getDisplayWidth() const;
  uint16_t getDisplayHeight() const;
  uint8_t getGrayscaleLevels() const;
  uint8_t* beginGrayscale16();
  bool commitGrayscale16();
  void cancelGrayscale16();
  uint16_t getDisplayWidthBytes() const;
  uint32_t getBufferSize() const;

 private:
  EInkDisplay einkDisplay;
};

extern HalDisplay display;
