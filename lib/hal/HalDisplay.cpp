#include <HalDisplay.h>
#include <HalGPIO.h>

#if FREEINK_DEVICE_READPICO
#include <BoardReadPico.h>
#include <Logging.h>
#endif

// Global HalDisplay instance
HalDisplay display;

#define SD_SPI_MISO 7

HalDisplay::HalDisplay() : einkDisplay(EPD_SCLK, EPD_MOSI, EPD_CS, EPD_DC, EPD_RST, EPD_BUSY) {}

HalDisplay::~HalDisplay() {}

void HalDisplay::begin(bool seamless) {
  // Set X3-specific panel mode before initializing.
  if (gpio.deviceIsX3()) {
    einkDisplay.setDisplayX3();
  }
#if FREEINK_DEVICE_READPICO
  // Precondition, not a gate: the SY7636A rails (EN P0.3, VCOM_EN P0.4), the EPD
  // output enable (XOE P0.1) and the PGOOD sense (P0.5) all live behind the
  // FCA9555, so the board must be up before the driver's power hooks can sequence
  // anything (BoardReadPico::epdPowerOn -> read-pico.md 1.4). HalGPIO::begin()
  // brings the expander up first; if it never answered, say so loudly and keep
  // going — the panel will simply stay dark instead of aborting the boot.
  if (!BoardReadPico::ready()) {
    LOG_ERR("DISP", "FCA9555 expander is not up; EPD rails cannot be sequenced");
  }
  // Watermarks for the unfalsified PSRAM/DMA budget (read-pico.md 2.6, B6/B7):
  // the step framebuffer (1.59 MiB), the gray canvas (812 KiB) and the 103,968-byte
  // facade framebuffer are PSRAM, while Panel_EPD's LUT expansion table is a
  // ~56.5 KiB INTERNAL DMA block whose allocation failure is silent. One line
  // before and after is what the hardware run needs to settle both rows.
  LOG_INF("DISP", "PSRAM free=%u maxBlock=%u internal free=%u maxBlock=%u before panel init",
          static_cast<unsigned>(ESP.getFreePsram()), static_cast<unsigned>(ESP.getMaxAllocPsram()),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#endif
#if FREEINK_DEVICE_MURPHY_M4
  einkDisplay.setMurphyM4Batch(gpio.murphyM4Batch());
#endif

  einkDisplay.begin();

#if FREEINK_DEVICE_READPICO
  // The facade publishes the driver's real geometry (LgfxEpdDriver::geometry():
  // 1216 x 684 -> 152 bytes/row -> 103,968 bytes for Read Pico). Every consumer
  // must read it from here; see the constants note in HalDisplay.h.
  LOG_INF("DISP", "panel %ux%u (%u bytes/row, %u-byte framebuffer); PSRAM free=%u maxBlock=%u internal free=%u",
          static_cast<unsigned>(getDisplayWidth()), static_cast<unsigned>(getDisplayHeight()),
          static_cast<unsigned>(getDisplayWidthBytes()), static_cast<unsigned>(getBufferSize()),
          static_cast<unsigned>(ESP.getFreePsram()), static_cast<unsigned>(ESP.getMaxAllocPsram()),
          static_cast<unsigned>(ESP.getFreeHeap()));
#endif

  if (seamless) {
    // Defuse the SDK's X3 _x3InitialFullSyncsRemaining counter (no-op on X4)
    // so the first paint isn't promoted to FULL (~770ms). Skips the wakeup-
    // gated requestResync() below for the same reason.
    einkDisplay.skipInitialResync();
    return;
  }
  // Request resync after specific wakeup events to ensure clean display state.
  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton || wakeupReason == HalGPIO::WakeupReason::AfterFlash ||
      wakeupReason == HalGPIO::WakeupReason::Other) {
    einkDisplay.requestResync();
  }
}

void HalDisplay::clearScreen(uint8_t color) const { einkDisplay.clearScreen(color); }

void HalDisplay::drawImage(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                           bool fromProgmem) const {
  einkDisplay.drawImage(imageData, x, y, w, h, fromProgmem);
}

void HalDisplay::drawImageTransparent(const uint8_t* imageData, uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                                      bool fromProgmem) const {
  einkDisplay.drawImageTransparent(imageData, x, y, w, h, fromProgmem);
}

EInkDisplay::RefreshMode convertRefreshMode(HalDisplay::RefreshMode mode) {
  switch (mode) {
    case HalDisplay::FULL_REFRESH:
      return EInkDisplay::FULL_REFRESH;
    case HalDisplay::HALF_REFRESH:
      return EInkDisplay::HALF_REFRESH;
    case HalDisplay::FAST_REFRESH:
    default:
      return EInkDisplay::FAST_REFRESH;
  }
}

namespace {
EInkDisplay::RefreshContext convertRefreshContext(DisplayRefreshContext context) {
  switch (context) {
    case DisplayRefreshContext::Normal:
      return EInkDisplay::RefreshContext::Normal;
    case DisplayRefreshContext::ContinuousReading:
      return EInkDisplay::RefreshContext::ContinuousReading;
    case DisplayRefreshContext::TextOnlyAntiAliasing:
      return EInkDisplay::RefreshContext::TextOnlyAntiAliasing;
    case DisplayRefreshContext::ImageReading:
      return EInkDisplay::RefreshContext::ImageReading;
  }
  return EInkDisplay::RefreshContext::Normal;
}
}  // namespace

void HalDisplay::displayBuffer(HalDisplay::RefreshMode mode, bool turnOffScreen, DisplayRefreshContext context) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBuffer(convertRefreshMode(mode), turnOffScreen, convertRefreshContext(context));
}

void HalDisplay::displayBufferAsync(HalDisplay::RefreshMode mode, DisplayRefreshContext context) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayBufferAsyncNoShadow(convertRefreshMode(mode), convertRefreshContext(context));
}

void HalDisplay::waitRefreshComplete() { einkDisplay.waitRefreshComplete(); }

bool HalDisplay::supportsAsyncRefresh() const { return einkDisplay.supportsAsyncRefresh(); }

void HalDisplay::refreshDisplay(HalDisplay::RefreshMode mode, bool turnOffScreen) {
  if (gpio.deviceIsX3() && mode == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.refreshDisplay(convertRefreshMode(mode), turnOffScreen);
}

void HalDisplay::setInverted(bool inverted) { einkDisplay.setInverted(inverted); }

bool HalDisplay::toggleInverted() { return einkDisplay.toggleInverted(); }

bool HalDisplay::isInverted() const { return einkDisplay.isInverted(); }

void HalDisplay::deepSleep() { einkDisplay.deepSleep(); }

uint8_t* HalDisplay::getFrameBuffer() const { return einkDisplay.getFrameBuffer(); }

uint8_t* HalDisplay::lendFrameBufferStorage(uint32_t* sizeOut) { return einkDisplay.lendBuildStorage(sizeOut); }

void HalDisplay::returnFrameBufferStorage() { einkDisplay.returnBuildStorage(); }

void HalDisplay::copyGrayscaleBuffers(const uint8_t* lsbBuffer, const uint8_t* msbBuffer) {
  einkDisplay.copyGrayscaleBuffers(lsbBuffer, msbBuffer);
}

void HalDisplay::displayGrayscaleBase(RefreshMode fallback, bool turnOffScreen, DisplayRefreshContext context) {
  // X3: a HALF fallback means the caller wants a clean base (e.g. the sleep
  // cover, a full-screen swap from arbitrary prior content). Without this, the
  // X3 grayscale base takes its gentle differential happy path and the prior
  // home/reader frame ghosts through the soft aa_pre_bw_mid waveform. Forcing a
  // resync makes displayGrayscaleBase clear first, matching displayBuffer(HALF).
  // The reader's FAST path is deliberately left on the differential path so
  // per-page grayscale stays cheap.
  if (gpio.deviceIsX3() && fallback == RefreshMode::HALF_REFRESH) {
    einkDisplay.requestResync(1);
  }

  einkDisplay.displayGrayscaleBase(convertRefreshMode(fallback), turnOffScreen, convertRefreshContext(context));
}

void HalDisplay::preconditionGrayscale() { einkDisplay.preconditionGrayscale(); }

void HalDisplay::preconditionGrayscale(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
  einkDisplay.preconditionGrayscale(x, y, w, h);
}

void HalDisplay::copyGrayscaleLsbBuffers(const uint8_t* lsbBuffer) { einkDisplay.copyGrayscaleLsbBuffers(lsbBuffer); }

void HalDisplay::copyGrayscaleMsbBuffers(const uint8_t* msbBuffer) { einkDisplay.copyGrayscaleMsbBuffers(msbBuffer); }

void HalDisplay::cleanupGrayscaleBuffers(const uint8_t* bwBuffer) { einkDisplay.cleanupGrayscaleBuffers(bwBuffer); }

void HalDisplay::displayGrayBuffer(bool turnOffScreen) { einkDisplay.displayGrayBuffer(turnOffScreen); }

void HalDisplay::writeGrayscalePlaneStrip(bool lsbPlane, const uint8_t* rows, uint16_t yStart, uint16_t numRows) {
  einkDisplay.writeGrayscalePlaneStrip(lsbPlane ? EInkDisplay::GRAY_PLANE_LSB : EInkDisplay::GRAY_PLANE_MSB, rows,
                                       yStart, numRows);
}

bool HalDisplay::supportsStripGrayscale() const { return einkDisplay.supportsStripGrayscale(); }

bool HalDisplay::combinesGrayscaleBase() const { return einkDisplay.combinesGrayscaleBase(); }

bool HalDisplay::supportsTextOnlyCombinedBase() const { return einkDisplay.supportsTextOnlyCombinedBase(); }
bool HalDisplay::supportsReaderTransitions() const { return einkDisplay.supportsReaderTransitions(); }
bool HalDisplay::supportsContinuousImageReading() const { return einkDisplay.supportsContinuousImageReading(); }
bool HalDisplay::canUseTextTransition() const { return einkDisplay.canUseTextTransition(); }

void HalDisplay::cancelGrayscale() {
  if (!einkDisplay.combinesGrayscaleBase()) return;
  einkDisplay.abortPostRefresh();
  // Discard staged planes and re-arm the next render without touching the glass.
  einkDisplay.beginDisplayWork();
}

uint16_t HalDisplay::getDisplayWidth() const { return einkDisplay.getDisplayWidth(); }

uint16_t HalDisplay::getDisplayHeight() const { return einkDisplay.getDisplayHeight(); }

uint16_t HalDisplay::getDisplayWidthBytes() const { return einkDisplay.getDisplayWidthBytes(); }

uint32_t HalDisplay::getBufferSize() const { return einkDisplay.getBufferSize(); }
