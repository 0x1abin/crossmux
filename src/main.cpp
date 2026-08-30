#include <Arduino.h>
#include <BoardConfig.h>
#include <Epub.h>
#include <FontCacheManager.h>
#include <FontDecompressor.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalDisplay.h>
#include <HalFrontlight.h>
#include <HalGPIO.h>
#include <HalOtaSlot.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <HalSystem.h>
#include <HalTiltSensor.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <SPI.h>
#include <WiFi.h>
#if FREEINK_CAP_TOUCH
#include <esp_sntp.h>
#endif
#if FREEINK_DEVICE_X4PRO
#include <XteinkDetect.h>
#endif
#include <builtinFonts/all.h>

#include <cstring>

#include "AchievementsStore.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "KOReaderCredentialStore.h"
#include "MappedInputManager.h"
#include "OpdsServerStore.h"
#include "ReadingStatsStore.h"
#include "RecentBooksStore.h"
#include "SdCardFontSystem.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#ifdef ENABLE_CHINESE_VERSION
#include "activities/settings/FontDownloadActivity.h"
#include "activities/settings/TextSettingsActivity.h"
#include "activities/util/ConfirmationActivity.h"
#endif
#include "activities/settings/LanguageSelectActivity.h"
#include "activities/settings/SdFirmwareUpdateActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "images/LoadingIcon.h"
#include "platform/UsbSerialJtagHandoff.h"
#include "util/ButtonNavigator.h"
#include "util/ScreenshotUtil.h"

GfxRenderer renderer(display);
MappedInputManager mappedInputManager(gpio, renderer);
ActivityManager activityManager(renderer, mappedInputManager);
FontDecompressor fontDecompressor;
SdCardFontSystem sdFontSystem;
FontCacheManager fontCacheManager(renderer.getFontMap(), renderer.getSdCardFonts());
static unsigned long allowSleepAt = 0;
constexpr unsigned long READING_STATS_CHECKPOINT_IDLE_MS = 15UL * 1000UL;
static unsigned long lastX4ProPowerClickAt = 0;

namespace {
constexpr unsigned long X4PRO_POWER_DOUBLE_CLICK_MS = 500;
constexpr unsigned long X4PRO_POWER_CLICK_MAX_HOLD_MS = 300;
}  // namespace

// A wake hold must never become an in-app power-button action.  Boot may continue
// while the button is held; swallow the one release that ends that wake gesture.
static bool wakePowerReleasePending = false;

// Fonts
// All legacy built-in reader IDs share one 12pt offline fallback. Complete
// families, other sizes, and style variants come from SD .cpfont files.
EpdFont offlineReaderFont(&notosans_cjk_12);
EpdFontFamily offlineReaderFontFamily(&offlineReaderFont);

// International UI fonts remain primary; CJK subsets are selected only when
// the primary is missing a Han glyph.
EpdFont smallFont(&notosans_8_regular);
EpdFontFamily smallFontFamily(&smallFont);

EpdFont ui10RegularFont(&ubuntu_10_regular);
EpdFont ui10BoldFont(&ubuntu_10_bold);
EpdFontFamily ui10FontFamily(&ui10RegularFont, &ui10BoldFont);

EpdFont ui12RegularFont(&ubuntu_12_regular);
EpdFont ui12BoldFont(&ubuntu_12_bold);
EpdFontFamily ui12FontFamily(&ui12RegularFont, &ui12BoldFont);

EpdFont cjk8Font(&notosans_cjk_8);
EpdFontFamily cjk8FontFamily(&cjk8Font);
EpdFont cjk10Font(&notosans_cjk_10);
EpdFontFamily cjk10FontFamily(&cjk10Font);
EpdFont cjk12Font(&notosans_cjk_12);
EpdFontFamily cjk12FontFamily(&cjk12Font);
constexpr int CJK_UI_8_FONT_ID = 0x434A4B08;
constexpr int CJK_UI_10_FONT_ID = 0x434A4B0A;
constexpr int CJK_UI_12_FONT_ID = 0x434A4B0C;

// Chinese chess piece glyphs (subset CJK font, 14 characters at 16pt).
EpdFont chineseChessPieceFont(&chinese_chess_16);
EpdFontFamily chineseChessPieceFontFamily(&chineseChessPieceFont);

// measurement of power button press duration calibration value
unsigned long t1 = 0;
unsigned long t2 = 0;

// Definitions for SilentRestart.h. RTC_NOINIT survives ESP.restart() but not power loss.
RTC_NOINIT_ATTR uint32_t silentRebootMagic;
RTC_NOINIT_ATTR uint32_t silentRebootTarget;
RTC_NOINIT_ATTR uint32_t silentRebootFontPointSize;
constexpr uint32_t SILENT_REBOOT_MAGIC = 0xC1EAB007;
enum class SilentRebootTarget : uint32_t {
  Home,
  Reader,
  ReaderSuppressFontPrompt,
  ReaderPreloadChineseFont,
  Count,
};

// How the device is coming back to life, resolved once at boot. Both resume
// flows suppress the splash and leave the panel holding its pre-boot frame; a
// plain boot shows the splash. See setup() for the resolution.
enum class BootResume : uint8_t {
  Splash,          // cold boot, flash, panic, or plain reboot
  Silent,          // heap-defrag ESP.restart() (RTC flag; lost on power loss)
  SplashlessWake,  // wake from deep sleep with the splash suppressed by the SD flag
};

// Latched true once enterDeepSleep() commits to sleeping, before it tears down
// the current activity. WiFi activities call silentRestart() in onExit() to
// clear heap fragmentation on the way out, but deep sleep is a full chip reset
// on wake and already clears the heap, so rebooting here would just power the
// device back up against the user's sleep gesture. Never cleared:
// startDeepSleep() does not return, so a set latch only ends at the wakeup reset.
static bool deepSleepInProgress = false;

#if FREEINK_CAP_TOUCH
static bool finishWifiSessionWithoutRestart() {
  if (!BoardConfig::hasTouch()) return false;
  if (esp_sntp_enabled()) esp_sntp_stop();
  WiFi.mode(WIFI_OFF);
  delay(100);
  LOG_DBG("MAIN", "WiFi stopped without restart on touch device");
  return true;
}
#endif

void silentRestart() {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = static_cast<uint32_t>(SilentRebootTarget::Home);
  silentRebootFontPointSize = 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=home)");
  // E-ink retains the previous frame until Home's first paint lands (~2-3s).
  // Without an overlay, users don't see the reboot and fire input through to
  // Home. Select on the default selectorIndex=0 then opens the most-recent
  // book, looking like a trampoline back to the reader they just exited.
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReader(const bool suppressChineseFontPrompt) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
#if FREEINK_CAP_TOUCH
  if (finishWifiSessionWithoutRestart()) return;
#endif
  silentRebootTarget = static_cast<uint32_t>(suppressChineseFontPrompt ? SilentRebootTarget::ReaderSuppressFontPrompt
                                                                       : SilentRebootTarget::Reader);
  silentRebootFontPointSize = 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader%s)", suppressChineseFontPrompt ? ", suppress-font-prompt" : "");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void silentRestartToReaderAndPreloadChineseFont(const uint8_t pointSize) {
  if (deepSleepInProgress) return;  // sleeping supersedes the heap-defrag reboot
  silentRebootTarget = static_cast<uint32_t>(SilentRebootTarget::ReaderPreloadChineseFont);
  silentRebootFontPointSize = pointSize;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Silent restart (target=reader-preload-font, size=%u)", static_cast<unsigned>(pointSize));
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  ESP.restart();
}

void restartToHomeAfterStorageHandoff() {
  if (deepSleepInProgress) return;  // sleeping supersedes the storage handoff reboot
  silentRebootTarget = static_cast<uint32_t>(SilentRebootTarget::Home);
  silentRebootFontPointSize = 0;
  silentRebootMagic = SILENT_REBOOT_MAGIC;
  LOG_DBG("MAIN", "Restart after storage handoff (target=home)");
  GUI.drawPopup(renderer, tr(STR_LOADING_POPUP));
  delay(50);
  handoffUsbOtgToSerialJtag();
  ESP.restart();
}

bool handleX4ProFrontlightDoubleClick() {
  if (!BoardConfig::isX4Pro() || !gpio.wasReleased(HalGPIO::BTN_POWER)) {
    return false;
  }

  const unsigned long now = millis();
  if (gpio.getPowerButtonHeldTime() > X4PRO_POWER_CLICK_MAX_HOLD_MS) {
    lastX4ProPowerClickAt = 0;
    return false;
  }
  if (lastX4ProPowerClickAt == 0 || now - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = now;
    return false;
  }

  lastX4ProPowerClickAt = 0;
  const bool lightOn = !Frontlight.isOn();
  Frontlight.setOn(lightOn);
  SETTINGS.frontlightOn = lightOn ? 1 : 0;
  SETTINGS.saveToFile();
  LOG_INF("LIGHT", "Frontlight toggled %s by power-button double-click", lightOn ? "on" : "off");
  return true;
}

void waitForPowerRelease() {
  gpio.update();
  while (gpio.isPressed(HalGPIO::BTN_POWER)) {
    delay(50);
    gpio.update();
  }
}

constexpr char SLEEP_FRAME_FILE[] = "/.crosspoint/sleep_frame.bin";

static void saveSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForWrite("SLP", SLEEP_FRAME_FILE, file)) return;
  file.write(renderer.getFrameBuffer(), renderer.getBufferSize());
  file.close();
}

static bool loadSleepFrameBuffer() {
  HalFile file;
  if (!Storage.openFileForRead("SLP", SLEEP_FRAME_FILE, file)) return false;
  const size_t bufferSize = display.getBufferSize();
  const size_t bytesRead = file.read(display.getFrameBuffer(), bufferSize);
  file.close();
  if (bytesRead != bufferSize) {
    Storage.remove(SLEEP_FRAME_FILE);
    return false;
  }
  Storage.remove(SLEEP_FRAME_FILE);
  return true;
}

// Enter deep sleep mode
void enterDeepSleep(bool fromTimeout = false) {
  HalPowerManager::Lock powerLock;  // Ensure we are at normal CPU frequency for sleep preparation
  APP_STATE.lastSleepFromReader = activityManager.isReaderActivity();

  const bool isQuickResumeSleep =
      SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME ||
      (fromTimeout &&
       SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT);
  // Every sleep mode leaves a complete retained frame on the e-ink panel. Keep
  // it visible until the first useful reader or home paint replaces it.
  APP_STATE.showBootScreen = false;

  APP_STATE.saveToFile();

  // Commit to sleeping before goToSleep() runs the outgoing activity's onExit():
  // a WiFi activity would otherwise silentRestart() here and reboot instead.
  deepSleepInProgress = true;
  activityManager.goToSleep(fromTimeout);

  if (!READING_STATS.saveToFile()) {
    LOG_ERR("RST", "Failed to save reading stats before deep sleep");
  }
  if (!ACHIEVEMENTS.saveToFile()) {
    LOG_ERR("ACH", "Failed to save achievements before deep sleep");
  }

  if (isQuickResumeSleep) {
    saveSleepFrameBuffer();
  } else if (Storage.exists(SLEEP_FRAME_FILE)) {
    // A stale Quick Resume frame must not replace the selected sleep screen during wake.
    Storage.remove(SLEEP_FRAME_FILE);
  }

  // Tear down WiFi so the modem power domain isn't held alive across deep sleep.
  // Wake from deep sleep is effectively a chip reset, so no state needs to survive.
  if (WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
  }

  halTiltSensor.deepSleep();
  Frontlight.setOn(false);
  display.deepSleep();
  LOG_DBG("MAIN", "Entering deep sleep");

  powerManager.startDeepSleep(gpio);
}

bool setupDisplayAndFonts(bool seamless = false, bool logSdFontLoadHeap = false) {
#if FREEINK_DEVICE_X4PRO
  // X4 Pro batches use SSD1677 or UC81xx. Resolve the controller before
  // display.begin(); C3 X3/X4 already do this once in HalGPIO::begin().
  static bool controllerResolved = false;
  if (!controllerResolved) {
    controllerResolved = true;
    freeink::applyXteinkDisplayController();
  }
#endif

  display.begin(seamless);
#if FREEINK_DEVICE_MURPHY_M4
  if (!gpio.restoreTouchAfterDisplayReset()) {
    LOG_ERR("MAIN", "Failed to restore Murphy M4 touch after display reset");
  }
#endif
  renderer.begin();
  activityManager.begin();
  LOG_DBG("MAIN", "Display initialized");

  // Initialize font decompressor for compressed reader fonts
  const bool fontDecompressorReady = fontDecompressor.init();
  if (!fontDecompressorReady) {
    LOG_ERR("MAIN", "Font decompressor init failed");
  }
  fontCacheManager.setFontDecompressor(&fontDecompressor);
  renderer.setFontCacheManager(&fontCacheManager);
  renderer.insertFont(NOTOSERIF_14_FONT_ID, offlineReaderFontFamily);
#ifndef OMIT_FONTS
  renderer.insertFont(NOTOSERIF_12_FONT_ID, offlineReaderFontFamily);
  renderer.insertFont(NOTOSERIF_16_FONT_ID, offlineReaderFontFamily);
  renderer.insertFont(NOTOSERIF_18_FONT_ID, offlineReaderFontFamily);

  renderer.insertFont(NOTOSANS_12_FONT_ID, offlineReaderFontFamily);
  renderer.insertFont(NOTOSANS_14_FONT_ID, offlineReaderFontFamily);
  renderer.insertFont(NOTOSANS_16_FONT_ID, offlineReaderFontFamily);
  renderer.insertFont(NOTOSANS_18_FONT_ID, offlineReaderFontFamily);
#endif  // OMIT_FONTS
  renderer.insertFont(UI_10_FONT_ID, ui10FontFamily);
  renderer.insertFont(UI_12_FONT_ID, ui12FontFamily);
  renderer.insertFont(SMALL_FONT_ID, smallFontFamily);
  renderer.insertFont(CJK_UI_8_FONT_ID, cjk8FontFamily);
  renderer.insertFont(CJK_UI_10_FONT_ID, cjk10FontFamily);
  renderer.insertFont(CJK_UI_12_FONT_ID, cjk12FontFamily);
  renderer.setFallbackFont(SMALL_FONT_ID, CJK_UI_8_FONT_ID);
  renderer.setFallbackFont(UI_10_FONT_ID, CJK_UI_10_FONT_ID);
  renderer.setFallbackFont(UI_12_FONT_ID, CJK_UI_12_FONT_ID);
  renderer.insertFont(BaseTheme::STATUS_NUMERIC_FONT_ID, smallFontFamily);
  renderer.insertFont(CHINESE_CHESS_FONT_ID, chineseChessPieceFontFamily);

  // Discover and load SD card fonts
  if (logSdFontLoadHeap) {
    LOG_INF("FONT", "Clean restart before font load: free=%u, maxAlloc=%u", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
  sdFontSystem.begin(renderer);

  LOG_DBG("MAIN", "Fonts setup");
  return fontDecompressorReady;
}

#ifdef ENABLE_CHINESE_VERSION
void continueChineseFontInstall(const uint8_t expectedPointSize) {
  if (APP_STATE.openEpubPath.empty() || !Storage.exists(APP_STATE.openEpubPath.c_str())) {
    LOG_ERR("FONT", "Cannot resume automatic font install: original EPUB is unavailable");
    activityManager.goHome();
    return;
  }

  const bool fontReady =
      strcmp(SETTINGS.sdFontFamilyName, SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY) == 0 &&
      SETTINGS.fontPointSize == expectedPointSize &&
      sdFontSystem.resolveFontId(SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY, expectedPointSize) != 0;
  if (!fontReady) {
    LOG_ERR("FONT", "Failed to load selected family %s at point size %u after clean restart",
            SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY, static_cast<unsigned>(expectedPointSize));
    SETTINGS.clearSdFontFamily();
    SETTINGS.fontPointSize = expectedPointSize;
    if (!SETTINGS.saveToFile()) LOG_ERR("FONT", "Failed to restore reader point size after font load failure");
  }

  activityManager.goToReader(APP_STATE.openEpubPath);
  activityManager.loop();
  if (!activityManager.isReaderActivity()) {
    LOG_ERR("FONT", "Cannot resume automatic font install: reader allocation failed");
    activityManager.goHome();
    return;
  }

  if (!fontReady) {
    auto error = makeUniqueNoThrow<FontDownloadActivity>(renderer, mappedInputManager,
                                                         FontDownloadActivity::Purpose::ReaderAutoInstall,
                                                         FontDownloadActivity::StartMode::ResumeFontLoadError);
    if (!error) {
      LOG_ERR("FONT", "OOM allocating resumed FontDownloadActivity (%zu bytes)", sizeof(FontDownloadActivity));
      return;
    }
    activityManager.pushActivity(std::move(error));
    activityManager.loop();
    return;
  }

  auto textSettings = makeUniqueNoThrow<TextSettingsActivity>(
      renderer, mappedInputManager, &sdFontSystem.registry(), TextSettingsActivity::Tab::Family,
      TextSettingsActivity::InitialFontState::Changed, TextSettingsActivity::StartMode::PreloadThenExit);
  if (textSettings) {
    activityManager.pushActivity(std::move(textSettings));
    activityManager.loop();
    return;
  }

  LOG_ERR("FONT", "OOM allocating automatic TextSettingsActivity (%zu bytes)", sizeof(TextSettingsActivity));
  SETTINGS.sdFontFlashPreload = 0;
  if (!SETTINGS.saveToFile()) LOG_ERR("FONT", "Failed to persist disabled font preload after OOM");

  auto notice = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInputManager, "", tr(STR_FONT_PRELOAD_FAILED),
                                                        ConfirmationActivity::BodyPlacement::PopupTitle);
  if (!notice) {
    LOG_ERR("FONT", "OOM allocating font preload failure notice (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  activityManager.pushActivity(std::move(notice));
  activityManager.loop();
}
#endif

void setup() {
  BoardConfig::holdPowerRails();

#ifdef ENABLE_SERIAL_LOG
#ifdef CROSSPOINT_WAIT_FOR_USB_SERIAL
  // Development builds preserve reliable early CDC logs; release builds let
  // enumeration proceed asynchronously so users do not pay this startup cost.
  delay(250);
#endif
  Serial.begin(115200);
#if LOG_SERIAL_HAS_TX_TIMEOUT
  logSerial.setTxTimeoutMs(1);  // This is a load-bearing 1. Do not modify.
#endif
#endif

  HalSystem::begin();
  const bool otaPendingAtBoot = HalOtaSlot::runningImageState() == HalOtaSlot::RunningImageState::PendingVerify;

  // Read-and-clear so a panic later in setup() doesn't loop into silent reboot.
  // Bound the target range too — RTC_NOINIT memory is uninitialized on cold boot.
  const bool isSilentReboot = (silentRebootMagic == SILENT_REBOOT_MAGIC);
  const bool targetIsValid = isSilentReboot && silentRebootTarget < static_cast<uint32_t>(SilentRebootTarget::Count);
  SilentRebootTarget snapshotTarget =
      targetIsValid ? static_cast<SilentRebootTarget>(silentRebootTarget) : SilentRebootTarget::Home;
  const bool fontPointSizeIsValid = snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont &&
                                    silentRebootFontPointSize > 0 && silentRebootFontPointSize <= UINT8_MAX;
  const uint8_t snapshotFontPointSize =
      snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont && fontPointSizeIsValid
          ? static_cast<uint8_t>(silentRebootFontPointSize)
          : 0;
  if (snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont && snapshotFontPointSize == 0) {
    snapshotTarget = SilentRebootTarget::Home;
  }
  silentRebootMagic = 0;
  silentRebootTarget = 0;
  silentRebootFontPointSize = 0;
#ifdef ENABLE_CHINESE_VERSION
  if (snapshotTarget == SilentRebootTarget::ReaderSuppressFontPrompt ||
      snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont) {
    FontDownloadActivity::suppressChineseFontPromptThisBoot();
  }
#endif

  gpio.begin();
  powerManager.begin();

  const auto wakeupReason = gpio.getWakeupReason();
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton && !gpio.verifyPowerButtonWakeup()) {
    LOG_DBG("MAIN", "Power-button wake not held through verification, sleeping");
    powerManager.startDeepSleep(gpio);
  }

  halTiltSensor.begin();
  halClock.begin();

  LOG_INF("MAIN", "Hardware detect: %s", BoardConfig::ACTIVE.name);

  bool recoveryFirmwareMode = false;
#if !FREEINK_DEVICE_PAPERMONO
  if (wakeupReason == HalGPIO::WakeupReason::PowerButton) {
    const unsigned long settleStart = millis();
    while (millis() - settleStart < 500) {
      gpio.update();
      delay(10);
    }
    const uint8_t recoveryButton =
        (BoardConfig::isX4Pro() || FREEINK_DEVICE_X4CLASSIC) ? HalGPIO::BTN_DOWN : HalGPIO::BTN_UP;
    recoveryFirmwareMode = gpio.isPressed(recoveryButton);
    if (recoveryFirmwareMode) {
      LOG_INF("MAIN", "Recovery firmware mode (%s + POWER held at boot)",
              (BoardConfig::isX4Pro() || FREEINK_DEVICE_X4CLASSIC) ? "DOWN" : "UP");
    }
  }
#endif

  // SD Card Initialization
  // We need 6 open files concurrently when parsing a new chapter
  if (!Storage.begin()) {
    LOG_ERR("MAIN", "SD card initialization failed");
    const bool fontsReady = setupDisplayAndFonts(isSilentReboot);
    activityManager.goToFullScreenMessage("SD card error", EpdFontFamily::BOLD);
    activityManager.requestUpdateAndWait();
    if (otaPendingAtBoot && fontsReady) {
      if (HalOtaSlot::confirmRunningImage()) {
        LOG_INF("OTA", "Running image confirmed after SD error display");
      } else {
        LOG_ERR("OTA", "Running image confirmation failed after SD error display");
      }
    }
    return;
  }

  HalSystem::checkPanic();

  if (gpio.hasTouch()) SETTINGS.readerMenuStyle = CrossPointSettings::READER_MENU_TOOLBAR;
  const bool settingsLoaded = SETTINGS.loadFromFile();
  const auto onboardingMode =
      settingsLoaded ? LanguageSelectActivity::Mode::Upgrade : LanguageSelectActivity::Mode::Initial;
  const bool requiresOnboarding = CrossPointSettings::requiresOnboarding(SETTINGS.onboardingVersion);
#ifndef SIMULATOR
  halClock.setUseChinaServers(SETTINGS.contentProfile == CrossPointSettings::ContentProfile::China);
#endif
  halClock.setAutoSyncEnabled(SETTINGS.clockAutoSync != 0);
  APP_STATE.loadFromFile();
  const bool isSleepWake = wakeupReason == HalGPIO::WakeupReason::PowerButton;
  const bool isPersistedSleepWake = isSleepWake && !APP_STATE.showBootScreen;

  RECENT_BOOKS.loadFromFile();
  READING_STATS.loadFromFile();
  ACHIEVEMENTS.loadFromFile();
  I18N.setLanguage(static_cast<Language>(SETTINGS.language));
  KOREADER_STORE.loadFromFile();
  OPDS_STORE.loadFromFile();
  UITheme::getInstance().reload();
  ButtonNavigator::setMappedInputManager(mappedInputManager);

  const bool restoreLightOn = SETTINGS.frontlightOn != 0 && (SETTINGS.frontlightRestoreOnWake != 0 || isSilentReboot);
  Frontlight.begin(SETTINGS.frontlightBrightness, SETTINGS.frontlightWarmth, restoreLightOn);

  switch (wakeupReason) {
    case HalGPIO::WakeupReason::PowerButton:
      LOG_DBG("MAIN", "Verifying power button press duration");
      if (!gpio.verifyPowerButtonWakeup(SETTINGS.getPowerButtonDuration(),
                                        SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP)) {
        powerManager.startDeepSleep(gpio);
      }
      wakePowerReleasePending = true;
      break;
    case HalGPIO::WakeupReason::AfterUSBPower:
      // Most devices return to sleep after a USB-powered cold boot.
      LOG_DBG("MAIN", "Wakeup reason: After USB Power");
#if FREEINK_DEVICE_X4PRO || FREEINK_DEVICE_X4CLASSIC || FREEINK_DEVICE_PAPERMONO || FREEINK_DEVICE_EEGO_A4
      // X4 Pro must stay awake so USB Serial/JTAG remains available after leaving
      // USB Drive and reconnecting the cable. Paper Mono has no armable GPIO wake
      // (its button is behind the PMIC). EEGO A4's post-flash reset reads as
      // POWERON (native-USB), so a flash would otherwise be misclassified as a
      // USB-power cold boot and sleep. Sleeping any of these here would strand
      // the device in a USB-replug boot loop (or sleep right after a flash).
      break;
#else
      powerManager.startDeepSleep(gpio);
      break;
#endif
    case HalGPIO::WakeupReason::AfterFlash:
      // After flashing, just proceed to boot
    case HalGPIO::WakeupReason::Other:
    default:
      break;
  }

  // First serial output only here to avoid timing inconsistencies for power button press duration verification
  LOG_DBG("MAIN", "Starting CrossPoint version " CROSSPOINT_VERSION);

  // Resolve the single boot-presentation decision. Skipping the splash also
  // skips the panel-clearing pass and the X3 initial-full-sync arming (see
  // HalDisplay::begin), so the first paint is FAST_REFRESH (~500ms) over the
  // retained frame and input dispatches against a visible UI.
  // Only a verified deep-sleep wake may use the one-shot persisted flag.
  // Otherwise a stale flag could suppress the splash on a cold boot.
  const BootResume resume = isSilentReboot         ? BootResume::Silent
                            : isPersistedSleepWake ? BootResume::SplashlessWake
                                                   : BootResume::Splash;
  bool allowFastInitialReaderRefresh = false;
  bool needsWakeRefresh = false;

  const bool fontsReady = setupDisplayAndFonts(resume != BootResume::Splash,
                                               snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont);
  const bool postOtaBoot = otaPendingAtBoot && fontsReady && activityManager.goToPostOtaBoot(!recoveryFirmwareMode);

  if (!postOtaBoot) {
    switch (resume) {
      case BootResume::Silent:
        // Splash skipped: the routing block below picks the target activity; the
        // panel keeps showing the pre-reboot popup until that first paint lands.
        break;
      case BootResume::SplashlessWake:
        // One-shot flag: re-arm the splash for the next ordinary boot. Save
        // before any painting so a hang in the blocking paint path can't strand
        // us in a splashless-with-no-frame loop on the next boot.
        APP_STATE.showBootScreen = true;
        APP_STATE.saveToFile();
        if (Storage.exists(SLEEP_FRAME_FILE) && loadSleepFrameBuffer()) {
          const bool useDifferentialRefresh = gpio.deviceIsX3();
          if (useDifferentialRefresh) {
            // begin() clears the X3 controller RAM, so restore the saved frame as
            // the baseline before replacing the moon with the loading icon.
            renderer.cleanupGrayscaleWithFrameBuffer();
          }

          const auto pageHeight = renderer.getScreenHeight();
          renderer.drawImage(LoadingIcon, 0, pageHeight - LOADINGICON_HEIGHT, LOADINGICON_WIDTH, LOADINGICON_HEIGHT);
          if (useDifferentialRefresh) {
            renderer.displayGrayscaleBase(HalDisplay::FAST_REFRESH);
            allowFastInitialReaderRefresh = true;
          } else {
            renderer.displayBuffer(HalDisplay::HALF_REFRESH);
          }
        } else {
          // The panel still physically shows the sleep image, so clean the
          // first Home paint without adding a separate refresh cycle.
          needsWakeRefresh = true;
        }
        break;
      case BootResume::Splash:
        activityManager.goToBoot();
        break;
    }
  }

  if (recoveryFirmwareMode) {
    // Skip normal home/reader routing: jump straight into the SD firmware picker.
    activityManager.replaceActivityWith<SdFirmwareUpdateActivity>(/*recoveryMode=*/true);
  } else if (HalSystem::isRebootFromPanic()) {
    // If we rebooted from a panic, go to crash report screen to show the panic info
    activityManager.goToCrashReport();
  } else if (postOtaBoot) {
    if (requiresOnboarding) {
      activityManager.replaceActivityWith<LanguageSelectActivity>(onboardingMode);
    } else {
      activityManager.goHome();
    }
  } else if (requiresOnboarding) {
    activityManager.replaceActivityWith<LanguageSelectActivity>(onboardingMode);
  } else if (resume == BootResume::Silent && snapshotTarget == SilentRebootTarget::ReaderPreloadChineseFont) {
#ifdef ENABLE_CHINESE_VERSION
    continueChineseFontInstall(snapshotFontPointSize);
#else
    activityManager.goHome();
#endif
  } else if (resume == BootResume::Silent &&
             (snapshotTarget == SilentRebootTarget::Reader ||
              snapshotTarget == SilentRebootTarget::ReaderSuppressFontPrompt) &&
             !APP_STATE.openEpubPath.empty()) {
    activityManager.goToReader(APP_STATE.openEpubPath);
  } else if (resume == BootResume::Silent) {
    // target == home (or reader with no open book): land on home — don't fall
    // through to the sleep-wake "resume reader" logic, which fires on stale
    // openEpubPath + lastSleepFromReader from a prior session.
    activityManager.goHome();
  } else if (APP_STATE.openEpubPath.empty() || !APP_STATE.lastSleepFromReader ||
             mappedInputManager.isPressed(MappedInputManager::Button::Back) || APP_STATE.readerActivityLoadCount > 0) {
    // Boot to home screen if no book is open, last sleep was not from reader, back button is held, or reader activity
    // crashed (indicated by readerActivityLoadCount > 0)
    if (needsWakeRefresh) renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
    activityManager.goHome();
  } else {
    // Clear app state to avoid getting into a boot loop if the epub doesn't load
    const auto path = APP_STATE.openEpubPath;
    APP_STATE.openEpubPath = "";
    APP_STATE.readerActivityLoadCount++;
    APP_STATE.saveToFile();
    // Splashless wake leaves the retained sleep frame on the panel; without a
    // clean first paint the reader shows the previous screen's residue (A4
    // grayscale panels ghost worst). Mirror the Home branch's HALF refresh so
    // reader resume also clears the retained frame.
    if (needsWakeRefresh) renderer.requestNextRefresh(HalDisplay::HALF_REFRESH);
    activityManager.goToReader(path, allowFastInitialReaderRefresh);
  }

  if (resume == BootResume::Silent) {
    if (postOtaBoot) {
      // Apply the queued Home replacement before waiting for its first physical paint.
      activityManager.loop();
    }
    // Block until the first paint physically completes. refreshDisplay()
    // waits on the panel BUSY pin so when this returns the user can see the
    // new activity. Without the wait, an edge captured by gpio.update()
    // during boot dispatches against an invisible Home and the default
    // selectorIndex=0 opens the most-recent book.
    activityManager.requestUpdateAndWait();
    // Absorb any button held at this point into currentState as a non-edge:
    // two gpio.update() calls separated by > InputManager's 5ms debounce
    // transition the held bit through lastDebounceTime into currentState
    // without setting pressedEvents, so the first loop()'s own gpio.update()
    // sees state == currentState and emits nothing.
    gpio.update();
    delay(10);
    gpio.update();
  }

  // Ensure we're not still holding the power button before leaving setup
  waitForPowerRelease();
  allowSleepAt = millis() + 2000;
}

void loop() {
  static unsigned long maxLoopDuration = 0;
  const unsigned long loopStartTime = millis();
  static unsigned long lastMemPrint = 0;

  gpio.setSharedConfirmPowerShortPressEmitsPower(SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP);
  mappedInputManager.update();

  if (activityManager.requiresExclusiveStorageLoop()) {
    // USB Drive handed the raw SD card to the host. Do not run screenshots,
    // sleep, shortcuts, or normal navigation while its filesystem is detached.
    activityManager.loop();
    if (activityManager.preventAutoSleep()) {
      powerManager.setPowerSaving(false);
      delay(10);
    } else {
      // No host is active, so a slower loop is safe. The activity itself times
      // out the raw-storage handoff rather than entering deep sleep detached.
      powerManager.setPowerSaving(true);
      delay(50);
    }
    return;
  }

  halTiltSensor.update(SETTINGS.tiltPageTurn, SETTINGS.orientation, activityManager.isReaderActivity());
  halClock.update();

  renderer.setFadingFix(SETTINGS.fadingFix);

  if (Serial && millis() - lastMemPrint >= 10000) {
    LOG_INF("MEM", "Free: %d bytes, Total: %d bytes, Min Free: %d bytes, MaxAlloc: %d bytes", ESP.getFreeHeap(),
            ESP.getHeapSize(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap());
    lastMemPrint = millis();
  }

  // Handle incoming serial commands,
  // nb: we use logSerial from logging to avoid deprecation warnings
  if (logSerial.available() > 0) {
    String line = logSerial.readStringUntil('\n');
    if (line.startsWith("CMD:")) {
      String cmd = line.substring(4);
      cmd.trim();
      if (cmd == "SCREENSHOT") {
        const uint32_t bufferSize = display.getBufferSize();
        logSerial.printf("SCREENSHOT_START:%d\n", bufferSize);
        uint8_t* buf = display.getFrameBuffer();
        logSerial.write(buf, bufferSize);
        logSerial.printf("SCREENSHOT_END\n");
      }
    }
  }

  // Check for any real user activity (button, touch, or tilt).
  static unsigned long lastActivityTime = millis();
  if (gpio.wasAnyPressed() || gpio.wasAnyReleased() || gpio.wasTouchActivity() || halTiltSensor.hadActivity()) {
    lastActivityTime = millis();         // Reset inactivity timer
    powerManager.setPowerSaving(false);  // Restore normal CPU frequency on user activity
  }
  // preventAutoSleep() is intentionally NOT folded into the activity check above:
  // it only short-circuits the deep-sleep timer below, not the inactivity clock
  // that drives auto-downclock. Standby (a clock face) wants deep sleep blocked
  // but still benefits from the framework dropping CPU to LOW_POWER_FREQ.

  if (wakePowerReleasePending && !gpio.isPressed(HalGPIO::BTN_POWER)) {
    wakePowerReleasePending = false;
    return;
  }

  static bool screenshotButtonsReleased = true;
  static bool screenshotComboActive = false;
  if (gpio.isPressed(HalGPIO::BTN_POWER) && gpio.isPressed(HalGPIO::BTN_DOWN)) {
    screenshotComboActive = true;
    if (screenshotButtonsReleased) {
      screenshotButtonsReleased = false;
      {
        RenderLock lock;
        ScreenshotUtil::takeScreenshot(renderer);
      }
    }
    return;
  }
  if (screenshotComboActive) {
    if (gpio.isPressed(HalGPIO::BTN_POWER)) return;
    if (gpio.wasReleased(HalGPIO::BTN_POWER)) {
      screenshotButtonsReleased = true;
      screenshotComboActive = false;
      return;
    }
    screenshotButtonsReleased = true;
    screenshotComboActive = false;
  }

  if (handleX4ProFrontlightDoubleClick()) return;

#if FREEINK_CAP_TOUCH
  mappedInputManager.setPowerConfirmClickFrame(false);
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::PWR_CONFIRM && BoardConfig::isX4Pro() &&
      lastX4ProPowerClickAt != 0 && millis() - lastX4ProPowerClickAt > X4PRO_POWER_DOUBLE_CLICK_MS) {
    lastX4ProPowerClickAt = 0;
    mappedInputManager.setPowerConfirmClickFrame(true);
  }
#endif

  const unsigned long sleepTimeoutMs = SETTINGS.getSleepTimeoutMs();
  if (sleepTimeoutMs > 0 && !activityManager.preventAutoSleep() && millis() - lastActivityTime >= sleepTimeoutMs) {
    LOG_DBG("SLP", "Auto-sleep triggered after %lu ms of inactivity", sleepTimeoutMs);
    enterDeepSleep(true);
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

  if (millis() >= allowSleepAt && gpio.isPressed(HalGPIO::BTN_POWER) &&
      gpio.getPowerButtonHeldTime() > SETTINGS.getPowerButtonDuration()) {
    // If the screenshot combination is potentially being pressed, don't sleep
    if (gpio.isPressed(HalGPIO::BTN_DOWN)) {
      return;
    }
    LOG_DBG("MAIN", "Power button held %lums, sleeping", gpio.getPowerButtonHeldTime());
    enterDeepSleep();
    // This should never be hit as `enterDeepSleep` calls esp_deep_sleep_start
    return;
  }

#if FREEINK_DEVICE_PAPERMONO
  // Paper Mono reports the PMIC power button as a one-tick click, so the held
  // path above cannot fire. With the default Ignore action, retain the normal
  // power-button meaning and shut down; explicit alternate bindings still win.
  if ((SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::SLEEP ||
       SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::IGNORE) &&
      millis() >= allowSleepAt && mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    enterDeepSleep();
    return;
  }
#endif

  // Refresh screen when power button is short-pressed with FORCE_REFRESH setting.
  if (SETTINGS.shortPwrBtn == CrossPointSettings::SHORT_PWRBTN::FORCE_REFRESH &&
      mappedInputManager.wasReleased(MappedInputManager::Button::Power)) {
    LOG_DBG("MAIN", "Manual screen refresh triggered");
    if (!activityManager.handleForcedRefresh()) {
      RenderLock lock;
      renderer.displayBuffer(HalDisplay::HALF_REFRESH);
    }
  }

  // Refresh the battery icon when USB is plugged or unplugged.
  // Placed after sleep guards so we never queue a render that won't be processed.
  if (gpio.wasUsbStateChanged()) {
    activityManager.requestUpdate();
  }
#ifndef CROSSPOINT_EMULATED
  if (gpio.wasInputModalityChanged() && gpio.hasTouch() && UITheme::getInstance().hasMainTabs() &&
      !activityManager.isReaderActivity()) {
    activityManager.requestUpdate();
  }
#endif

  const unsigned long activityStartTime = millis();
  const bool readerWasActive = activityManager.isReaderActivity();
  activityManager.loop();
  const bool readerIsActive = activityManager.isReaderActivity();
  const unsigned long activityDuration = millis() - activityStartTime;

  if (readerWasActive && !readerIsActive) {
    if (!READING_STATS.saveToFile()) {
      LOG_ERR("RST", "Failed to save reading stats after reader exit");
    }
    if (!ACHIEVEMENTS.saveToFile()) {
      LOG_ERR("ACH", "Failed to save achievements after reader exit");
    }
  } else if (readerIsActive && (millis() - lastActivityTime) >= READING_STATS_CHECKPOINT_IDLE_MS &&
             !activityManager.skipLoopDelay() && !activityManager.preventAutoSleep() &&
             READING_STATS.shouldSaveCheckpoint()) {
    RenderLock lock;
    if (!READING_STATS.saveToFile()) {
      LOG_ERR("RST", "Failed to save idle reading checkpoint");
    }
  }

  const unsigned long loopDuration = millis() - loopStartTime;
  if (loopDuration > maxLoopDuration) {
    maxLoopDuration = loopDuration;
    if (maxLoopDuration > 50) {
      LOG_DBG("LOOP", "New max loop duration: %lu ms (activity: %lu ms)", maxLoopDuration, activityDuration);
    }
  }

  // Add delay at the end of the loop to prevent tight spinning
  // When an activity requests skip loop delay (e.g., webserver running), use yield() for faster response
  // Otherwise, use longer delay to save power
  if (activityManager.skipLoopDelay()) {
    powerManager.setPowerSaving(false);  // Make sure we're at full performance when skipLoopDelay is requested
    yield();                             // Give FreeRTOS a chance to run tasks, but return immediately
  } else {
    if (millis() - lastActivityTime >= HalPowerManager::IDLE_POWER_SAVING_MS) {
      // If we've been inactive for a while, increase the delay to save power
      powerManager.setPowerSaving(true);  // Lower CPU frequency after extended inactivity
      delay(50);
    } else {
      // Short delay to prevent tight loop while still being responsive
      delay(10);
    }
  }
}
