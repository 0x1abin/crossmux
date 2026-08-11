#pragma once

#include <Arduino.h>
#include <BatteryMonitor.h>
#include <EInkDisplay.h>
#include <FrontlightManager.h>
#include <InputManager.h>
#include <Rtc.h>
#include <SDCardManager.h>

#include <array>
#include <initializer_list>

#include "CpFontRenderer.h"
#include "PortraitCanvas.h"

class DiagnosticApp {
 public:
  DiagnosticApp(EInkDisplay& display, InputManager& input, BatteryMonitor& battery,
                FrontlightManager& frontlight, Rtc& rtc, SDCardManager& sd);

  void begin();
  void loop();

 private:
  enum class TestState : uint8_t { Unknown, Running, Pass, Warn, Fail, NotFitted };
  enum class View : uint8_t { Menu, Report, Detail, Display, Fonts, Touch, Buttons };
  enum TestId : uint8_t {
    Display,
    Fonts,
    Touch,
    Buttons,
    Storage,
    Battery,
    RtcClock,
    Wifi,
    Ble,
    I2cSensors,
    Frontlight,
    Memory,
    Usb,
    TestCount
  };

  struct TestResult {
    const char* name;
    TestState state = TestState::Unknown;
    String detail;
  };

  struct MenuItem {
    const char* label;
    void (DiagnosticApp::*action)();
  };

  enum class ChargePhase : uint8_t { BatteryOnly, Charging, Full, Count };

  struct ChargeCapture {
    bool captured = false;
    uint16_t millivolts = 0;
    int8_t rawLevel = -1;
  };

  EInkDisplay& display_;
  InputManager& input_;
  BatteryMonitor& battery_;
  FrontlightManager& frontlight_;
  Rtc& rtc_;
  SDCardManager& sd_;
  PortraitCanvas canvas_;
  CpFontRenderer fontRenderer_;

  std::array<TestResult, TestCount> results_{};
  View view_ = View::Menu;
  uint8_t selected_ = 0;
  uint16_t touchedCells_ = 0;
  uint8_t buttonMask_ = 0;
  bool frontShortSeen_ = false;
  bool frontLongSeen_ = false;
  uint8_t displayPhase_ = 0;
  uint8_t displayVisitedMask_ = 0;
  uint8_t fontPage_ = 0;
  uint8_t fontVisitedMask_ = 0;
  bool fontPackReady_ = false;
  uint32_t lastLiveDraw_ = 0;
  String serialLine_;
  String i2cAddresses_;
  String wifiSummary_;
  String bleSummary_;
  String detailTitle_;
  String detailSubtitle_;
  std::array<TestId, 4> detailTests_{};
  uint8_t detailTestCount_ = 0;
  void (DiagnosticApp::*detailRerun_)() = nullptr;
  bool detailChargeCaptureMode_ = false;
  std::array<ChargeCapture, static_cast<size_t>(ChargePhase::Count)>
      chargeCaptures_{};

  static constexpr uint8_t MENU_COUNT = 11;
  static constexpr int16_t MENU_TOP = 126;
  static constexpr int16_t MENU_ROW_HEIGHT = 52;
  static constexpr uint8_t DISPLAY_PHASE_COUNT = 6;
  static constexpr uint8_t FONT_PAGE_COUNT = 3;
  static const std::array<MenuItem, MENU_COUNT> MENU;

  void setResult(TestId id, TestState state, const String& detail);
  static const char* stateLabel(TestState state);
  static char stateMark(TestState state);

  void refresh(EInkDisplay::RefreshMode mode = EInkDisplay::FAST_REFRESH);
  void drawHeader(const char* title, const char* subtitle = nullptr);
  void drawFooter(const char* text);
  void drawWrapped(const String& text, int16_t x, int16_t y, int16_t width, uint8_t textSize = 2);
  void showMessage(const char* title, const String& body, bool full = false);
  void renderMenu(bool full = false);
  void renderTouchTest(bool full = false);
  void renderButtonTest(bool full = false);
  void renderReportScreen();
  void renderDetailScreen(bool full = false);
  void renderSafeAreaTest();
  void renderDisplayPattern(uint8_t variant);
  void renderSolidDisplay(bool black);
  void renderFontPage(uint8_t page);
  void showTestDetail(const char* title, const char* subtitle,
                      std::initializer_list<TestId> tests,
                      void (DiagnosticApp::*rerun)(),
                      bool chargeCaptureMode = false);
  void showDisplayPhase(uint8_t phase);
  void showFontPage(uint8_t page);

  void handleMenuInput();
  void handleReportInput();
  void handleDetailInput();
  void handleDisplayInput();
  void handleFontInput();
  void handleTouchInput();
  void handleButtonInput();
  void handleSerial();
  void executeSerialCommand(String command);
  void runMenuItem(uint8_t index);
  void returnToMenu();

  void runAll();
  void runOverview();
  void enterTouchTest();
  void enterButtonTest();
  void runDisplayTest();
  void runFontTest();
  void runStorageRtcTest();
  void runPowerTest();
  void runWifiTest();
  void runBleTest();
  void runBusSensorTest();
  void runMemoryTest();

  void probeBasicHardware();
  void testDisplay();
  void testGrayscale();
  void testFonts();
  void testStorage();
  void testBattery();
  void testInputIdleLevels();
  void testRtc();
  void testWifiScan();
  void testWifiConnect(const String& credentials);
  void testBleScan();
  void testI2c();
  void testFrontlight(uint8_t brightness = 10, uint8_t warm = 50);
  void testMemory();
  void testUsb();
  void setRtcFromCommand(const String& value);
  void timedDeepSleep(uint32_t seconds);
  void captureChargePhase(const String& phase);
  void resetChargeCaptures();
  bool chargeCapturesComplete() const;
  bool chargeCapturePatternValid() const;
  static const char* chargePhaseLabel(ChargePhase phase);

  void printHelp() const;
  void printStatus() const;
  void printUiState() const;
  void printJsonReport() const;
  static String jsonEscape(const String& value);
};
