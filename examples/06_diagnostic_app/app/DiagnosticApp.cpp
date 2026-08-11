#include "DiagnosticApp.h"

#include <BoardConfig.h>
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <Wire.h>
#include <esp_heap_caps.h>

#include <algorithm>
#include <cstring>
#include <iterator>

#include <EegoA4Support.h>

#include "EegoA4Hardware.h"

namespace {

constexpr size_t SERIAL_SCREENSHOT_CHUNK = 512;
constexpr unsigned long SERIAL_SCREENSHOT_TIMEOUT_MS = 20000;

size_t writeSerialFully(const uint8_t* data, const size_t size, const unsigned long timeoutMs) {
  size_t offset = 0;
  unsigned long lastProgressAt = millis();
  while (offset < size && millis() - lastProgressAt < timeoutMs) {
    const size_t requested = std::min(SERIAL_SCREENSHOT_CHUNK, size - offset);
    const size_t written = Serial.write(data + offset, requested);
    if (written > 0) {
      offset += written;
      lastProgressAt = millis();
    } else {
      delay(1);
    }
    yield();
  }
  return offset;
}

}  // namespace

const std::array<DiagnosticApp::MenuItem, DiagnosticApp::MENU_COUNT> DiagnosticApp::MENU = {{
    {"Run all automated tests", &DiagnosticApp::runAll},
    {"Hardware overview", &DiagnosticApp::runOverview},
    {"Touch grid test", &DiagnosticApp::enterTouchTest},
    {"Buttons + front key", &DiagnosticApp::enterButtonTest},
    {"E-paper waveforms", &DiagnosticApp::runDisplayTest},
    {"Fonts: Chinese + English", &DiagnosticApp::runFontTest},
    {"SD card + RTC", &DiagnosticApp::runStorageRtcTest},
    {"Battery + charging", &DiagnosticApp::runPowerTest},
    {"Wi-Fi radio scan", &DiagnosticApp::runWifiTest},
    {"Bluetooth LE scan", &DiagnosticApp::runBleTest},
    {"I2C, sensors + memory", &DiagnosticApp::runBusSensorTest},
}};

DiagnosticApp::DiagnosticApp(EInkDisplay& display, InputManager& input, BatteryMonitor& battery,
                             FrontlightManager& frontlight, Rtc& rtc, SDCardManager& sd)
    : display_(display),
      input_(input),
      battery_(battery),
      frontlight_(frontlight),
      rtc_(rtc),
      sd_(sd),
      canvas_(display),
      fontRenderer_(sd, canvas_) {
  results_[Display].name = "Display";
  results_[Fonts].name = "Fonts zh-CN/en";
  results_[Touch].name = "Touch/front key";
  results_[Buttons].name = "Side buttons";
  results_[Storage].name = "microSD";
  results_[Battery].name = "Battery/charge";
  results_[RtcClock].name = "PCF8563 RTC";
  results_[Wifi].name = "Wi-Fi";
  results_[Ble].name = "Bluetooth LE";
  results_[I2cSensors].name = "I2C/sensors";
  results_[Frontlight].name = "LM3630A option";
  results_[Memory].name = "Flash/PSRAM";
  results_[Usb].name = "USB CDC";
}

void DiagnosticApp::begin() {
  probeBasicHardware();
  renderMenu(true);
  printHelp();
}

void DiagnosticApp::loop() {
  handleSerial();
  input_.update();

  switch (view_) {
    case View::Menu: handleMenuInput(); break;
    case View::Report: handleReportInput(); break;
    case View::Detail: handleDetailInput(); break;
    case View::Display: handleDisplayInput(); break;
    case View::Fonts: handleFontInput(); break;
    case View::Touch: handleTouchInput(); break;
    case View::Buttons: handleButtonInput(); break;
  }
  delay(input_.isDebouncePending() ? 2 : 8);
}

void DiagnosticApp::setResult(const TestId id, const TestState state, const String& detail) {
  results_[id].state = state;
  results_[id].detail = detail;
  Serial.printf("[TEST] %-18s %-10s %s\n", results_[id].name, stateLabel(state), detail.c_str());
}

const char* DiagnosticApp::stateLabel(const TestState state) {
  switch (state) {
    case TestState::Unknown: return "NOT_RUN";
    case TestState::Running: return "RUNNING";
    case TestState::Pass: return "PASS";
    case TestState::Warn: return "WARN";
    case TestState::Fail: return "FAIL";
    case TestState::NotFitted: return "NOT_FITTED";
  }
  return "UNKNOWN";
}

char DiagnosticApp::stateMark(const TestState state) {
  switch (state) {
    case TestState::Unknown: return '?';
    case TestState::Running: return '>';
    case TestState::Pass: return '+';
    case TestState::Warn: return '!';
    case TestState::Fail: return 'X';
    case TestState::NotFitted: return '-';
  }
  return '?';
}

void DiagnosticApp::refresh(const EInkDisplay::RefreshMode mode) {
  const uint32_t started = millis();
  display_.displayBuffer(mode, false);
  Serial.printf("[DISPLAY] refresh=%s elapsed=%lums\n",
                mode == EInkDisplay::FULL_REFRESH
                    ? "full"
                    : (mode == EInkDisplay::HALF_REFRESH ? "half" : "fast"),
                static_cast<unsigned long>(millis() - started));
}

void DiagnosticApp::drawHeader(const char* title, const char* subtitle) {
  canvas_.useUiContentRect();
  canvas_.setTextWrap(false);
  canvas_.setTextColor(0, 1);
  canvas_.setTextSize(3);
  // Ordinary UI uses the common zero-radius rectangle. Equal visual padding
  // keeps the title away from the top without the old R48-driven x=60 indent.
  canvas_.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_Y + 4);
  canvas_.print(title);
  canvas_.drawFastHLine(eego::UI_CONTENT_X, 64, eego::UI_CONTENT_WIDTH, 0);
  if (subtitle) {
    canvas_.setTextSize(1);
    canvas_.setCursor(eego::UI_CONTENT_X + 4, 74);
    canvas_.print(subtitle);
  }
}

void DiagnosticApp::drawFooter(const char* text) {
  canvas_.useUiContentRect();
  canvas_.drawFastHLine(eego::UI_CONTENT_X, eego::UI_CONTENT_BOTTOM - 23,
                        eego::UI_CONTENT_WIDTH, 0);
  canvas_.setTextColor(0, 1);
  canvas_.setTextSize(1);
  canvas_.setCursor(eego::UI_CONTENT_X + 4, eego::UI_CONTENT_BOTTOM - 13);
  canvas_.print(text);
}

void DiagnosticApp::drawWrapped(const String& text, const int16_t x, int16_t y, const int16_t width,
                                const uint8_t textSize) {
  canvas_.setTextSize(textSize);
  canvas_.setTextColor(0, 1);
  const int charsPerLine = std::max(1, width / (6 * static_cast<int>(textSize)));
  int start = 0;
  while (start < static_cast<int>(text.length()) &&
         y < eego::UI_CONTENT_BOTTOM - 36) {
    int end = std::min(start + charsPerLine, static_cast<int>(text.length()));
    const int newline = text.indexOf('\n', start);
    if (newline >= start && newline < end) {
      end = newline;
    } else if (end < static_cast<int>(text.length())) {
      const int space = text.lastIndexOf(' ', end);
      if (space > start) end = space;
    }
    canvas_.setCursor(x, y);
    canvas_.print(text.substring(start, end));
    y += 9 * textSize;
    start = end;
    while (start < static_cast<int>(text.length()) &&
           (text[start] == ' ' || text[start] == '\n' || text[start] == '\r')) {
      ++start;
    }
  }
}

void DiagnosticApp::showMessage(const char* title, const String& body, const bool full) {
  canvas_.useUiContentRect();
  canvas_.clear();
  drawHeader(title, "EEGO A4 non-destructive hardware diagnostics");
  drawWrapped(body, eego::UI_CONTENT_X + 4, 118,
              eego::UI_CONTENT_WIDTH - 8, 2);
  drawFooter("USB serial remains active. Type 'help' for commands.");
  refresh(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
}

void DiagnosticApp::renderMenu(const bool full) {
  canvas_.useUiContentRect();
  canvas_.clear();
  const String subtitle = String("Template firmware ") + eego::TEMPLATE_VERSION +
                          " | persistent test pages | no automatic sleep";
  drawHeader("EEGO A4 LAB", subtitle.c_str());

  const auto& battery = results_[Battery];
  canvas_.setTextSize(1);
  canvas_.setTextColor(0, 1);
  canvas_.setCursor(eego::UI_CONTENT_X + 4, 102);
  canvas_.printf("SD:%c Touch:%c Battery:%c RTC:%c  Free heap:%lu KB",
                 stateMark(results_[Storage].state), stateMark(results_[Touch].state),
                 stateMark(battery.state), stateMark(results_[RtcClock].state),
                 static_cast<unsigned long>(ESP.getFreeHeap() / 1024));

  for (uint8_t i = 0; i < MENU_COUNT; ++i) {
    const int16_t y = MENU_TOP + i * MENU_ROW_HEIGHT;
    TestState state = TestState::Unknown;
    switch (i) {
      case 0: state = TestState::Unknown; break;
      case 1: state = results_[Memory].state; break;
      case 2: state = results_[Touch].state; break;
      case 3: state = results_[Buttons].state; break;
      case 4: state = results_[Display].state; break;
      case 5: state = results_[Fonts].state; break;
      case 6: state = results_[Storage].state == TestState::Fail
                          ? TestState::Fail
                          : results_[RtcClock].state;
        break;
      case 7: state = results_[Battery].state; break;
      case 8: state = results_[Wifi].state; break;
      case 9: state = results_[Ble].state; break;
      case 10: state = results_[I2cSensors].state; break;
    }

    if (i == selected_) {
      canvas_.fillRect(eego::UI_CONTENT_X, y, eego::UI_CONTENT_WIDTH,
                       MENU_ROW_HEIGHT - 3, 0);
      canvas_.setTextColor(1, 0);
    } else {
      canvas_.drawRect(eego::UI_CONTENT_X, y, eego::UI_CONTENT_WIDTH,
                       MENU_ROW_HEIGHT - 3, 0);
      canvas_.setTextColor(0, 1);
    }
    canvas_.setTextSize(2);
    canvas_.setCursor(eego::UI_CONTENT_X + 12, y + 16);
    canvas_.printf("[%c] %s", stateMark(state), MENU[i].label);
  }
  drawFooter("UP/DOWN select | POWER run | touch row | bottom key back");
  refresh(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
}

void DiagnosticApp::renderTouchTest(const bool full) {
  canvas_.useUiContentRect();
  canvas_.clear();
  drawHeader("TOUCH GRID", "Tap all nine cells. Bottom key short press exits.");

  // Full-panel exception: this grid deliberately tests whether touch reaches
  // all physical edges. It is diagnostic geometry, never body/UI content.
  canvas_.useFullPanel();
  const int16_t top = 100;
  const int16_t gridHeight = eego::LOGICAL_HEIGHT - top - 62;
  const int16_t cellW = eego::LOGICAL_WIDTH / 3;
  const int16_t cellH = gridHeight / 3;
  for (uint8_t row = 0; row < 3; ++row) {
    for (uint8_t col = 0; col < 3; ++col) {
      const uint8_t cell = row * 3 + col;
      const int16_t x = col * cellW;
      const int16_t y = top + row * cellH;
      canvas_.drawRect(x + 2, y + 2, cellW - 4, cellH - 4, 0);
      const int16_t cx = x + cellW / 2;
      const int16_t cy = y + cellH / 2;
      if (touchedCells_ & (1U << cell)) {
        canvas_.fillCircle(cx, cy, 22, 0);
      } else {
        canvas_.drawCircle(cx, cy, 22, 0);
        canvas_.drawFastHLine(cx - 30, cy, 60, 0);
        canvas_.drawFastVLine(cx, cy - 30, 60, 0);
      }
    }
  }
  canvas_.useUiContentRect();
  drawFooter("Expected logical range: X=0..551, Y=0..767");
  refresh(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
}

void DiagnosticApp::renderButtonTest(const bool full) {
  canvas_.useUiContentRect();
  canvas_.clear();
  drawHeader("BUTTON TEST", "Press UP, DOWN, POWER; short and long-press the bottom key.");

  struct Row {
    const char* name;
    bool hit;
  };
  const Row rows[] = {
      {"GPIO5 UP (active-low)", (buttonMask_ & 0x01U) != 0},
      {"GPIO7 DOWN (active-low)", (buttonMask_ & 0x02U) != 0},
      {"GPIO8 POWER (active-high)", (buttonMask_ & 0x04U) != 0},
      {"Bottom key SHORT = Back", frontShortSeen_},
      {"Bottom key HOLD 700ms = Home", frontLongSeen_},
  };

  for (uint8_t i = 0; i < 5; ++i) {
    const int16_t y = 120 + i * 92;
    canvas_.drawRoundRect(eego::UI_CONTENT_X, y, eego::UI_CONTENT_WIDTH,
                          68, 8, 0);
    if (rows[i].hit) canvas_.fillCircle(62, y + 34, 15, 0);
    else canvas_.drawCircle(62, y + 34, 15, 0);
    canvas_.setTextColor(0, 1);
    canvas_.setTextSize(2);
    canvas_.setCursor(98, y + 25);
    canvas_.print(rows[i].name);
  }
  drawFooter("Tap top-left to exit; page stays visible after all inputs pass.");
  refresh(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
}

void DiagnosticApp::renderReportScreen() {
  view_ = View::Report;
  canvas_.useUiContentRect();
  canvas_.clear();
  drawHeader("TEST REPORT", "PASS + | WARN ! | FAIL X | NOT FITTED - | NOT RUN ?");
  for (uint8_t i = 0; i < TestCount; ++i) {
    const int16_t y = 96 + i * 47;
    canvas_.setTextColor(0, 1);
    canvas_.setTextSize(2);
    canvas_.setCursor(eego::UI_CONTENT_X + 4, y);
    canvas_.printf("[%c] %s", stateMark(results_[i].state), results_[i].name);
    canvas_.setTextSize(1);
    canvas_.setCursor(eego::UI_CONTENT_X + 16, y + 22);
    String detail = results_[i].detail;
    if (detail.length() > 76) detail = detail.substring(0, 73) + "...";
    canvas_.print(detail);
  }
  drawFooter("Bottom key returns to menu | serial: report json");
  refresh(EInkDisplay::FULL_REFRESH);
}

void DiagnosticApp::showTestDetail(const char* title, const char* subtitle,
                                   const std::initializer_list<TestId> tests,
                                   void (DiagnosticApp::*rerun)(),
                                   const bool chargeCaptureMode) {
  detailTitle_ = title;
  detailSubtitle_ = subtitle;
  detailTestCount_ = 0;
  for (const TestId id : tests) {
    if (detailTestCount_ >= detailTests_.size()) break;
    detailTests_[detailTestCount_++] = id;
  }
  detailRerun_ = rerun;
  detailChargeCaptureMode_ = chargeCaptureMode;
  view_ = View::Detail;
  renderDetailScreen(true);
}

void DiagnosticApp::renderDetailScreen(const bool full) {
  canvas_.useUiContentRect();
  canvas_.clear();
  drawHeader(detailTitle_.c_str(), detailSubtitle_.c_str());

  constexpr int16_t top = 100;
  constexpr int16_t bottom = 650;
  const uint8_t count = std::max<uint8_t>(1, detailTestCount_);
  const int16_t cardHeight = static_cast<int16_t>((bottom - top) / count);
  for (uint8_t i = 0; i < detailTestCount_; ++i) {
    const TestResult& result = results_[detailTests_[i]];
    const int16_t y = top + i * cardHeight;
    canvas_.drawRoundRect(eego::UI_CONTENT_X, y + 4,
                          eego::UI_CONTENT_WIDTH, cardHeight - 10, 8, 0);
    canvas_.setTextColor(0, 1);
    canvas_.setTextSize(2);
    canvas_.setCursor(eego::UI_CONTENT_X + 12, y + 24);
    canvas_.printf("[%c] %s", stateMark(result.state), result.name);
    canvas_.setTextSize(1);
    String detail = result.detail.isEmpty() ? String("No result detail.") : result.detail;
    const int charsPerLine = 78;
    const int maxLines = std::max(1, (cardHeight - 58) / 11);
    int offset = 0;
    for (int line = 0; line < maxLines && offset < static_cast<int>(detail.length()); ++line) {
      int end = std::min(offset + charsPerLine, static_cast<int>(detail.length()));
      if (end < static_cast<int>(detail.length())) {
        const int space = detail.lastIndexOf(' ', end);
        if (space > offset) end = space;
      }
      canvas_.setCursor(eego::UI_CONTENT_X + 16, y + 54 + line * 11);
      canvas_.print(detail.substring(offset, end));
      offset = end;
      while (offset < static_cast<int>(detail.length()) && detail[offset] == ' ') ++offset;
    }
  }

  constexpr int16_t buttonY = 660;
  constexpr int16_t buttonHeight = 52;
  canvas_.setTextColor(0, 1);
  if (detailChargeCaptureMode_) {
    constexpr int16_t gap = 8;
    constexpr int16_t buttonWidth = 160;
    const char* labels[] = {"BATTERY", "CHARGING", "FULL"};
    for (uint8_t i = 0; i < 3; ++i) {
      const int16_t x =
          eego::UI_CONTENT_X + i * (buttonWidth + gap);
      canvas_.drawRoundRect(x, buttonY, buttonWidth, buttonHeight, 8, 0);
      canvas_.setTextSize(i == 1 ? 1 : 2);
      canvas_.setCursor(x + (i == 1 ? 56 : 38),
                        buttonY + (i == 1 ? 22 : 18));
      canvas_.print(labels[i]);
    }
    canvas_.setTextSize(1);
    canvas_.setCursor(eego::UI_CONTENT_X + 4, 728);
    canvas_.print(
        "UP=BATTERY | DOWN=CHARGING | POWER=FULL | bottom key=BACK");
  } else {
    constexpr int16_t buttonWidth = 238;
    constexpr int16_t rightButtonX = eego::UI_CONTENT_X + buttonWidth + 20;
    canvas_.drawRoundRect(eego::UI_CONTENT_X, buttonY, buttonWidth,
                          buttonHeight, 8, 0);
    canvas_.drawRoundRect(rightButtonX, buttonY, buttonWidth,
                          buttonHeight, 8, 0);
    canvas_.setTextSize(2);
    canvas_.setCursor(eego::UI_CONTENT_X + 95, buttonY + 18);
    canvas_.print("BACK");
    canvas_.setCursor(rightButtonX + (detailRerun_ ? 83 : 95),
                      buttonY + 18);
    canvas_.print(detailRerun_ ? "RETEST" : "MENU");
    canvas_.setTextSize(1);
    canvas_.setCursor(eego::UI_CONTENT_X + 4, 728);
    canvas_.print(
        "Bottom key: back | Power: retest | page remains until input");
  }
  refresh(full ? EInkDisplay::FULL_REFRESH : EInkDisplay::FAST_REFRESH);
}

void DiagnosticApp::renderSafeAreaTest() {
  // Full-panel exception: this page exists to expose the physical aperture and
  // must reach pixel 0. Restore UiContentRect before explanatory text.
  canvas_.useFullPanel();
  canvas_.clear();

  // The black band touches all four framebuffer edges and grows inward. Its
  // outer R=60 geometry defines the display safe-area boundary; the inner
  // white R=48 region begins after the complete 12 px band. Measurements use
  // logical 552x768 pixels and rotate one-for-one onto the physical panel.
  for (int16_t inset = 0; inset < eego::SAFE_CONTENT_INSET; ++inset) {
    canvas_.drawRoundRect(
        inset, inset, eego::LOGICAL_WIDTH - inset * 2,
        eego::LOGICAL_HEIGHT - inset * 2,
        eego::DISPLAY_OUTER_RADIUS - inset, 0);
  }

  canvas_.useUiContentRect();
  // Show the second-layer, zero-radius UI rectangle as a one-pixel guide. This
  // is the default clip used by every ordinary page.
  canvas_.drawRect(eego::UI_CONTENT_X, eego::UI_CONTENT_Y,
                   eego::UI_CONTENT_WIDTH, eego::UI_CONTENT_HEIGHT, 0);
  canvas_.setTextColor(0, 1);
  canvas_.setTextSize(3);
  canvas_.setCursor(123, 258);
  canvas_.print("DISPLAY SAFE AREA");
  canvas_.setTextSize(2);
  canvas_.setCursor(162, 330);
  canvas_.print("EDGE BORDER = 12 PX");
  canvas_.setCursor(156, 366);
  canvas_.print("OUTER RADIUS = 60 PX");
  canvas_.setCursor(138, 402);
  canvas_.print("ROUNDED SAFE = R48");
  canvas_.setCursor(126, 438);
  canvas_.print("UI RECT = 496 X 712");
  canvas_.setCursor(84, 486);
  canvas_.print("RECT INSET = 28 PX / ZERO RADIUS");
  canvas_.setTextSize(1);
  canvas_.setCursor(105, 720);
  canvas_.print(
      "POWER/DOWN/tap next | UP previous | bottom key exits");
}

void DiagnosticApp::renderDisplayPattern(const uint8_t variant) {
  // Full-panel exception for panel-edge and waveform geometry.
  canvas_.useFullPanel();
  canvas_.clear();
  canvas_.drawRect(0, 0, eego::LOGICAL_WIDTH, eego::LOGICAL_HEIGHT, 0);
  canvas_.drawRect(8, 8, eego::LOGICAL_WIDTH - 16, eego::LOGICAL_HEIGHT - 16, 0);

  if (variant == 0) {
    for (int16_t y = 130; y < 550; y += 48) {
      canvas_.drawFastHLine(24, y, eego::LOGICAL_WIDTH - 48, 0);
    }
    for (int16_t x = 24; x < eego::LOGICAL_WIDTH - 24; x += 48) {
      canvas_.drawFastVLine(x, 130, 420, 0);
    }
  } else {
    constexpr int16_t tile = 32;
    for (int16_t y = 128; y < 640; y += tile) {
      for (int16_t x = 20; x < eego::LOGICAL_WIDTH - 20; x += tile) {
        if (((x / tile) + (y / tile)) & 1) canvas_.fillRect(x, y, tile, tile, 0);
      }
    }
  }

  // Labels and controls are ordinary foreground content and are clipped to
  // the calibrated R48 content area.
  canvas_.useUiContentRect();
  canvas_.setTextColor(0, 1);
  canvas_.setTextSize(3);
  canvas_.setCursor(eego::UI_CONTENT_X + 4, 32);
  canvas_.printf("UC8279C TEST %u", variant);
  canvas_.setTextSize(2);
  canvas_.setCursor(36, 78);
  canvas_.print("768x600 scan / 768x552 framebuffer");
  if (variant == 0) {
    canvas_.fillRect(30, 590, 150, 90, 0);
    canvas_.drawRect(201, 590, 150, 90, 0);
    canvas_.setTextSize(2);
    canvas_.setTextColor(1, 0);
    canvas_.setCursor(67, 626);
    canvas_.print("BLACK");
    canvas_.setTextColor(0, 1);
    canvas_.setCursor(242, 626);
    canvas_.print("WHITE");
  } else {
    canvas_.fillRect(30, 674, eego::LOGICAL_WIDTH - 60, 44, 1);
    canvas_.drawRect(30, 674, eego::LOGICAL_WIDTH - 60, 44, 0);
    canvas_.setTextSize(2);
    canvas_.setTextColor(0, 1);
    canvas_.setCursor(50, 688);
    canvas_.print("FAST DIFFERENTIAL CHANGE");
  }
  drawFooter("POWER/tap next | UP previous | bottom key exits");
}

void DiagnosticApp::renderSolidDisplay(const bool black) {
  // Full-screen black/white is the test background. All labels revert to the
  // rectangular UI content clip immediately after the clear.
  canvas_.useFullPanel();
  canvas_.clear(black ? 0 : 1);
  canvas_.useUiContentRect();
  canvas_.setTextColor(black ? 1 : 0, black ? 0 : 1);
  canvas_.setTextSize(4);
  canvas_.setCursor(105, 305);
  canvas_.print(black ? "BLACK FIELD" : "WHITE FIELD");
  canvas_.setTextSize(2);
  canvas_.setCursor(82, 372);
  canvas_.print("Inspect uniformity and stuck pixels");
  canvas_.drawRoundRect(38, 660, eego::LOGICAL_WIDTH - 76, 62, 8, black ? 1 : 0);
  canvas_.setCursor(96, 682);
  canvas_.print("POWER/tap next | bottom exits");
}

void DiagnosticApp::showDisplayPhase(uint8_t phase) {
  if (phase >= DISPLAY_PHASE_COUNT) phase = DISPLAY_PHASE_COUNT - 1;
  displayPhase_ = phase;
  displayVisitedMask_ |= static_cast<uint8_t>(1U << displayPhase_);
  view_ = View::Display;
  const uint32_t started = millis();
  switch (displayPhase_) {
    case 0:
      setResult(Display, TestState::Running,
                "stage 1/6 safe area: edge border=12, outer R60, rounded R48, UI rect=28/496x712");
      renderSafeAreaTest();
      refresh(EInkDisplay::FULL_REFRESH);
      break;
    case 1:
      setResult(Display, TestState::Running,
                "stage 2/6 full grid displayed; inspect borders and line geometry");
      renderDisplayPattern(0);
      refresh(EInkDisplay::FULL_REFRESH);
      break;
    case 2:
      setResult(Display, TestState::Running,
                "stage 3/6 fast checker displayed; inspect differential residue");
      renderDisplayPattern(1);
      refresh(EInkDisplay::FAST_REFRESH);
      break;
    case 3:
      setResult(Display, TestState::Running,
                "stage 4/6 four grayscale bit-plane codes displayed");
      testGrayscale();
      break;
    case 4:
      setResult(Display, TestState::Running,
                "stage 5/6 black field displayed; inspect uniformity and stuck white pixels");
      renderSolidDisplay(true);
      refresh(EInkDisplay::FULL_REFRESH);
      break;
    case 5:
      setResult(Display, TestState::Warn,
                "all six patterns displayed; visual confirmation required before PASS");
      renderSolidDisplay(false);
      refresh(EInkDisplay::FULL_REFRESH);
      break;
  }
  Serial.printf("[DISPLAY] interactive stage=%u/%u elapsed=%lums; waiting for user input\n",
                static_cast<unsigned>(displayPhase_ + 1),
                static_cast<unsigned>(DISPLAY_PHASE_COUNT),
                static_cast<unsigned long>(millis() - started));
}

void DiagnosticApp::renderFontPage(const uint8_t page) {
  canvas_.useUiContentRect();
  canvas_.clear();
  const String subtitle =
      String("MiSansA4 CPFONT v4 from SD /fonts/MiSansA4 | page ") +
      (page + 1) + "/" + FONT_PAGE_COUNT;
  drawHeader("FONT TEST", subtitle.c_str());

  uint16_t pageMissing = 0;
  uint16_t pageIoErrors = 0;
  if (page < 2) {
    static constexpr uint8_t sizes[] = {8, 10, 12, 14, 16, 18};
    constexpr int16_t top = 100;
    constexpr int16_t cardHeight = 190;
    for (uint8_t row = 0; row < 3; ++row) {
      const uint8_t pointSize = sizes[page * 3 + row];
      const int16_t y = top + row * cardHeight;
      canvas_.drawRoundRect(eego::UI_CONTENT_X, y + 3,
                            eego::UI_CONTENT_WIDTH,
                            cardHeight - 12, 8, 0);
      canvas_.setTextColor(0, 1);
      canvas_.setTextSize(1);
      canvas_.setCursor(eego::UI_CONTENT_X + 8, y + 13);
      canvas_.printf("MiSansA4 %u pt | regular | CPFONT 2-bit", pointSize);

      char path[64] = {};
      snprintf(path, sizeof(path), "/fonts/MiSansA4/MiSansA4_%u.cpfont",
               pointSize);
      if (!fontRenderer_.load(path)) {
        ++pageIoErrors;
        canvas_.setTextSize(2);
        canvas_.setCursor(eego::UI_CONTENT_X + 12, y + 62);
        canvas_.printf("LOAD FAILED: %s", fontRenderer_.error().c_str());
        continue;
      }

      uint16_t missing = 0;
      uint16_t ioErrors = 0;
      // The full coverage string fits through 16 pt. Keep the 18 pt sample on
      // one line so that it remains fully visible inside the diagnostic card;
      // page 3 retains the complete 0-9 coverage sample.
      const char* englishSample =
          pointSize >= 18 ? "English: ABC xyz 0123"
                          : "English: ABC xyz 0123456789";
      fontRenderer_.drawText(eego::UI_CONTENT_X + 8, y + 36,
                             englishSample, &missing, &ioErrors);
      pageMissing += missing;
      pageIoErrors += ioErrors;
      fontRenderer_.drawText(eego::UI_CONTENT_X + 8,
                             y + 42 + fontRenderer_.info().advanceY,
                             "中文显示：你好，世界！", &missing, &ioErrors);
      pageMissing += missing;
      pageIoErrors += ioErrors;

      canvas_.setTextSize(1);
      canvas_.setCursor(eego::UI_CONTENT_X + 8, y + cardHeight - 31);
      canvas_.printf("%lu glyphs | %lu intervals | line=%u px",
                     static_cast<unsigned long>(
                         fontRenderer_.info().glyphCount),
                     static_cast<unsigned long>(
                         fontRenderer_.info().intervalCount),
                     fontRenderer_.info().advanceY);
    }
  } else {
    const char* lines[] = {
        "English coverage / MiSans Regular",
        "The quick brown fox jumps over",
        "13 lazy dogs. ABC xyz 0123456789",
        "中文覆盖 / 简体中文",
        "你好，世界！电子纸阅读器",
        "中英文混排：EEGO A4 字体测试",
        "标点：，。！？；：“”（）【】《》",
        "Symbols: @#%&+-= / Wi-Fi BLE",
    };
    constexpr uint8_t pointSize = 14;
    constexpr int16_t firstLineY = 108;
    const char* path = "/fonts/MiSansA4/MiSansA4_14.cpfont";
    if (fontRenderer_.load(path)) {
      const int16_t step = fontRenderer_.info().advanceY + 8;
      for (uint8_t index = 0; index < std::size(lines); ++index) {
        uint16_t missing = 0;
        uint16_t ioErrors = 0;
        fontRenderer_.drawText(eego::UI_CONTENT_X + 4,
                               firstLineY + index * step, lines[index],
                               &missing, &ioErrors);
        pageMissing += missing;
        pageIoErrors += ioErrors;
        if (index == 2 || index == 5) {
          const int16_t ruleY =
              firstLineY + index * step + fontRenderer_.info().advanceY + 2;
          canvas_.drawFastHLine(eego::UI_CONTENT_X, ruleY,
                                eego::UI_CONTENT_WIDTH, 0);
        }
      }
      canvas_.drawRoundRect(eego::UI_CONTENT_X, 552,
                            eego::UI_CONTENT_WIDTH, 112, 8, 0);
      canvas_.setTextSize(1);
      canvas_.setTextColor(0, 1);
      canvas_.setCursor(36, 570);
      canvas_.printf("Required language coverage: English + Simplified Chinese");
      canvas_.setCursor(36, 590);
      canvas_.printf("Font file: %lu bytes, %lu glyphs, %lu intervals",
                     static_cast<unsigned long>(
                         fontRenderer_.info().fileBytes),
                     static_cast<unsigned long>(
                         fontRenderer_.info().glyphCount),
                     static_cast<unsigned long>(
                         fontRenderer_.info().intervalCount));
      canvas_.setCursor(36, 610);
      canvas_.printf("This page uses CPFONT pixels, not Adafruit ASCII.");
      canvas_.setCursor(36, 630);
      canvas_.printf("Missing on page: %u | bitmap I/O errors: %u",
                     pageMissing, pageIoErrors);
    } else {
      ++pageIoErrors;
      drawWrapped(String("Cannot load ") + path + ": " + fontRenderer_.error(),
                  eego::UI_CONTENT_X + 4, 130,
                  eego::UI_CONTENT_WIDTH - 8, 2);
    }
  }
  fontRenderer_.unload();

  if (pageMissing || pageIoErrors) {
    fontPackReady_ = false;
    setResult(Fonts, TestState::Fail,
              String("font render failed: page=") + (page + 1) +
                  " missing=" + pageMissing +
                  " bitmap_io_errors=" + pageIoErrors);
  }

  drawFooter(page + 1 < FONT_PAGE_COUNT
                 ? "POWER/DOWN/tap next | UP previous | bottom key exits"
                 : "POWER confirms all pages | UP previous | bottom key exits");
  refresh(EInkDisplay::FULL_REFRESH);
  Serial.printf(
      "[FONT] page=%u/%u missing=%u io_errors=%u waiting_for_input=1\n",
      static_cast<unsigned>(page + 1), static_cast<unsigned>(FONT_PAGE_COUNT),
      pageMissing, pageIoErrors);
}

void DiagnosticApp::showFontPage(uint8_t page) {
  if (page >= FONT_PAGE_COUNT) page = FONT_PAGE_COUNT - 1;
  fontPage_ = page;
  fontVisitedMask_ |= static_cast<uint8_t>(1U << fontPage_);
  view_ = View::Fonts;
  renderFontPage(fontPage_);
}

void DiagnosticApp::handleMenuInput() {
  bool redraw = false;
  if (input_.wasHomeKeyShortPressed()) {
    renderMenu(false);
    return;
  }
  if (input_.wasPressed(InputManager::BTN_UP)) {
    selected_ = selected_ == 0 ? MENU_COUNT - 1 : selected_ - 1;
    redraw = true;
  }
  if (input_.wasPressed(InputManager::BTN_DOWN)) {
    selected_ = static_cast<uint8_t>((selected_ + 1) % MENU_COUNT);
    redraw = true;
  }
  if (input_.wasPressed(InputManager::BTN_POWER)) {
    runMenuItem(selected_);
    return;
  }
  if (input_.wasHomeKeyLongPressed()) {
    runAll();
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (input_.wasTouchTap(nx, ny)) {
    const int16_t logicalX = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
    const int16_t logicalY = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
    (void)logicalX;
    if (logicalY >= MENU_TOP) {
      const int row = (logicalY - MENU_TOP) / MENU_ROW_HEIGHT;
      if (row >= 0 && row < MENU_COUNT) {
        selected_ = static_cast<uint8_t>(row);
        runMenuItem(selected_);
        return;
      }
    }
  }
  if (redraw) renderMenu(false);
}

void DiagnosticApp::handleReportInput() {
  if (input_.wasHomeKeyShortPressed() || input_.wasHomeKeyLongPressed() ||
      input_.wasPressed(InputManager::BTN_POWER)) {
    returnToMenu();
    return;
  }
  float nx = 0.0f;
  float ny = 0.0f;
  if (input_.wasTouchTap(nx, ny)) {
    const int16_t x = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
    const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
    if ((x < 140 && y < 110) || y > 700) returnToMenu();
  }
}

void DiagnosticApp::handleDetailInput() {
  if (input_.wasHomeKeyShortPressed() || input_.wasHomeKeyLongPressed()) {
    returnToMenu();
    return;
  }
  if (detailChargeCaptureMode_) {
    if (input_.wasPressed(InputManager::BTN_UP)) {
      captureChargePhase("battery");
      renderDetailScreen(false);
      return;
    }
    if (input_.wasPressed(InputManager::BTN_DOWN)) {
      captureChargePhase("charging");
      renderDetailScreen(false);
      return;
    }
    if (input_.wasPressed(InputManager::BTN_POWER)) {
      captureChargePhase("full");
      renderDetailScreen(false);
      return;
    }

    float nx = 0.0f;
    float ny = 0.0f;
    if (!input_.wasTouchTap(nx, ny)) return;
    const int16_t x = static_cast<int16_t>(
        eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
    const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
    if ((x < 135 && y < 105) || y > 735) {
      returnToMenu();
      return;
    }
    if (y >= 655) {
      const uint8_t phase =
          std::min<uint8_t>(2, x * 3 / eego::LOGICAL_WIDTH);
      captureChargePhase(
          chargePhaseLabel(static_cast<ChargePhase>(phase)));
      renderDetailScreen(false);
    }
    return;
  }
  if (input_.wasPressed(InputManager::BTN_POWER)) {
    if (detailRerun_) {
      (this->*detailRerun_)();
    } else {
      returnToMenu();
    }
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (!input_.wasTouchTap(nx, ny)) return;
  const int16_t x = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
  const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
  if (y < 655) return;
  if (x < eego::LOGICAL_WIDTH / 2) {
    returnToMenu();
  } else if (detailRerun_) {
    (this->*detailRerun_)();
  } else {
    returnToMenu();
  }
}

void DiagnosticApp::handleDisplayInput() {
  if (input_.wasHomeKeyShortPressed() || input_.wasHomeKeyLongPressed()) {
    setResult(Display, TestState::Warn,
              String("interactive display test exited at stage ") +
                  (displayPhase_ + 1) + "/" + DISPLAY_PHASE_COUNT);
    returnToMenu();
    return;
  }
  if (input_.wasPressed(InputManager::BTN_UP)) {
    showDisplayPhase(displayPhase_ == 0 ? 0 : displayPhase_ - 1);
    return;
  }

  bool advance = input_.wasPressed(InputManager::BTN_POWER) ||
                 input_.wasPressed(InputManager::BTN_DOWN);
  float nx = 0.0f;
  float ny = 0.0f;
  if (input_.wasTouchTap(nx, ny)) advance = true;
  if (!advance) return;

  if (displayPhase_ + 1 < DISPLAY_PHASE_COUNT) {
    showDisplayPhase(displayPhase_ + 1);
    return;
  }
  const uint8_t allDisplayPhases =
      static_cast<uint8_t>((1U << DISPLAY_PHASE_COUNT) - 1U);
  const bool allVisited = displayVisitedMask_ == allDisplayPhases;
  setResult(Display, allVisited ? TestState::Pass : TestState::Warn,
            allVisited ? "R60/R48/28px-rect safe area + Full/Fast/Gray/Black/White acknowledged; inspect visual quality"
                       : String("display sequence ended with visited mask=0x") +
                             String(displayVisitedMask_, HEX) + "; run full interactive sequence");
  showTestDetail("DISPLAY RESULT", "Six patterns completed; this page remains until Back or Retest.",
                 {Display}, &DiagnosticApp::runDisplayTest);
}

void DiagnosticApp::handleFontInput() {
  if (input_.wasHomeKeyShortPressed() || input_.wasHomeKeyLongPressed()) {
    returnToMenu();
    return;
  }
  if (input_.wasPressed(InputManager::BTN_UP)) {
    showFontPage(fontPage_ == 0 ? 0 : fontPage_ - 1);
    return;
  }

  bool advance = input_.wasPressed(InputManager::BTN_POWER) ||
                 input_.wasPressed(InputManager::BTN_DOWN);
  float nx = 0.0f;
  float ny = 0.0f;
  if (input_.wasTouchTap(nx, ny)) {
    const int16_t x =
        static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
    const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
    if ((x < 135 && y < 105) || y > 700) {
      returnToMenu();
      return;
    }
    advance = true;
  }
  if (!advance) return;

  if (fontPage_ + 1 < FONT_PAGE_COUNT) {
    showFontPage(fontPage_ + 1);
    return;
  }

  const uint8_t allPages =
      static_cast<uint8_t>((1U << FONT_PAGE_COUNT) - 1U);
  if (fontPackReady_ && fontVisitedMask_ == allPages) {
    setResult(
        Fonts, TestState::Pass,
        "MiSansA4 8/10/12/14/16/18 loaded; English and Chinese glyphs rendered; all 3 pages acknowledged");
  } else if (results_[Fonts].state != TestState::Fail) {
    setResult(Fonts, TestState::Warn,
              String("font pages visited mask=0x") +
                  String(fontVisitedMask_, HEX) +
                  "; visual confirmation incomplete");
  }
  Serial.println("[FONT] final page retained; bottom key returns to menu");
}

void DiagnosticApp::handleTouchInput() {
  if (input_.wasHomeKeyShortPressed() || input_.wasHomeKeyLongPressed()) {
    const bool complete = touchedCells_ == 0x01ffU;
    setResult(Touch, complete ? TestState::Pass : TestState::Warn,
              complete ? "all 9 logical regions + controller detected"
                       : String("controller detected; regions hit=") + __builtin_popcount(touchedCells_) + "/9");
    returnToMenu();
    return;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (!input_.wasTouchTap(nx, ny)) return;
  const int16_t x = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
  const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
  const int16_t top = 100;
  const int16_t gridHeight = eego::LOGICAL_HEIGHT - top - 62;
  if (y >= top && y < top + gridHeight) {
    const uint8_t col = std::min<uint8_t>(2, x * 3 / eego::LOGICAL_WIDTH);
    const uint8_t row = std::min<uint8_t>(2, (y - top) * 3 / gridHeight);
    touchedCells_ |= static_cast<uint16_t>(1U << (row * 3 + col));
    Serial.printf("[TOUCH] logical=(%d,%d) native_norm=(%.4f,%.4f) cells=0x%03x\n", x, y, nx, ny,
                  touchedCells_);
    if (touchedCells_ == 0x01ffU) {
      setResult(Touch, TestState::Pass, "all 9 logical regions hit; orientation/range passed");
    }
    renderTouchTest(false);
  }
}

void DiagnosticApp::handleButtonInput() {
  bool changed = false;
  if (input_.wasPressed(InputManager::BTN_UP)) {
    buttonMask_ |= 0x01U;
    changed = true;
  }
  if (input_.wasPressed(InputManager::BTN_DOWN)) {
    buttonMask_ |= 0x02U;
    changed = true;
  }
  if (input_.wasPressed(InputManager::BTN_POWER)) {
    buttonMask_ |= 0x04U;
    changed = true;
  }
  if (input_.wasHomeKeyShortPressed()) {
    frontShortSeen_ = true;
    changed = true;
  }
  if (input_.wasHomeKeyLongPressed()) {
    frontLongSeen_ = true;
    changed = true;
  }

  float nx = 0.0f;
  float ny = 0.0f;
  if (input_.wasTouchTap(nx, ny)) {
    const int16_t x = static_cast<int16_t>(eego::PANEL_HEIGHT - 1 - ny * eego::PANEL_HEIGHT);
    const int16_t y = static_cast<int16_t>(nx * eego::PANEL_WIDTH);
    if (x < 120 && y < 120) {
      const bool complete = buttonMask_ == 0x07U && frontShortSeen_ && frontLongSeen_;
      setResult(Buttons, complete ? TestState::Pass : TestState::Warn,
                complete ? "UP/DOWN/POWER and front short/long observed" : "manual test exited before all inputs");
      returnToMenu();
      return;
    }
  }

  if (!changed) return;
  const bool complete = buttonMask_ == 0x07U && frontShortSeen_ && frontLongSeen_;
  setResult(Buttons, complete ? TestState::Pass : TestState::Running,
            String("side mask=0x") + String(buttonMask_, HEX) + " short=" + frontShortSeen_ +
                " long=" + frontLongSeen_);
  renderButtonTest(false);
}

void DiagnosticApp::handleSerial() {
  if (Serial.available() && results_[Usb].state != TestState::Pass) {
    setResult(Usb, TestState::Pass, "USB CDC host-to-device command received at 115200 baud");
  }
  while (Serial.available()) {
    const char ch = static_cast<char>(Serial.read());
    if (ch == '\r') continue;
    if (ch == '\n') {
      serialLine_.trim();
      if (!serialLine_.isEmpty()) executeSerialCommand(serialLine_);
      serialLine_ = "";
      continue;
    }
    if (serialLine_.length() < 255) serialLine_ += ch;
  }
}

void DiagnosticApp::executeSerialCommand(String command) {
  command.trim();
  String lower = command;
  lower.toLowerCase();
  if (lower.startsWith("cmd:")) {
    command = command.substring(4);
    command.trim();
    lower = command;
    lower.toLowerCase();
  }
  Serial.printf("> %s\n", command.c_str());

  if (lower == "help") {
    printHelp();
  } else if (lower == "status") {
    printStatus();
  } else if (lower == "ui state") {
    printUiState();
  } else if (lower == "report json") {
    printJsonReport();
  } else if (lower == "screenshot") {
    const uint32_t bufferSize = display_.getBufferSize();
    const uint8_t* framebuffer = display_.getFrameBuffer();
    if (!framebuffer || bufferSize == 0) {
      Serial.println("SCREENSHOT_ERROR:NO_FRAMEBUFFER");
    } else {
      Serial.printf("SCREENSHOT_START:%lu\n", static_cast<unsigned long>(bufferSize));
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
      Serial.setTxTimeoutMs(eego::USB_CDC_SCREENSHOT_TX_TIMEOUT_MS);
#endif
      const size_t written =
          writeSerialFully(framebuffer, bufferSize, SERIAL_SCREENSHOT_TIMEOUT_MS);
      if (written == bufferSize) {
        Serial.println("SCREENSHOT_END");
      } else {
        Serial.printf("SCREENSHOT_ERROR:%u:%lu\n", static_cast<unsigned>(written),
                      static_cast<unsigned long>(bufferSize));
      }
#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
      // Keep the long timeout through the footer and drain it before normal
      // non-blocking diagnostics resume.
      Serial.flush();
      Serial.setTxTimeoutMs(eego::USB_CDC_NORMAL_TX_TIMEOUT_MS);
#endif
    }
  } else if (lower == "menu") {
    returnToMenu();
  } else if (lower == "run all") {
    runAll();
  } else if (lower == "run overview") {
    runOverview();
  } else if (lower == "run display") {
    runDisplayTest();
  } else if (lower == "run font" || lower == "run fonts") {
    runFontTest();
  } else if (lower.startsWith("font page ")) {
    const int requested = command.substring(10).toInt();
    if (requested < 1 || requested > FONT_PAGE_COUNT) {
      Serial.printf("font page must be 1..%u\n", FONT_PAGE_COUNT);
    } else {
      testFonts();
      fontVisitedMask_ = 0;
      showFontPage(static_cast<uint8_t>(requested - 1));
    }
  } else if (lower == "run safe" || lower == "screen safe") {
    showDisplayPhase(0);
  } else if (lower == "run gray" || lower == "screen gray") {
    showDisplayPhase(3);
  } else if (lower == "run touch") {
    enterTouchTest();
  } else if (lower == "run buttons") {
    enterButtonTest();
  } else if (lower == "run sd") {
    testStorage();
    showTestDetail("SD CARD RESULT", "Result remains visible; Power retests SD + RTC.",
                   {Storage}, &DiagnosticApp::runStorageRtcTest);
  } else if (lower == "run battery") {
    testBattery();
    showTestDetail(
        "BATTERY + CHARGE",
        "UP=battery, DOWN=charging, POWER=full; samples stay in RAM.",
        {Battery}, &DiagnosticApp::runPowerTest, true);
  } else if (lower.startsWith("charge capture ")) {
    captureChargePhase(lower.substring(15));
    showTestDetail(
        "CHARGE PHASES",
        "UP=battery, DOWN=charging, POWER=full; samples stay in RAM.",
        {Battery}, &DiagnosticApp::runPowerTest, true);
  } else if (lower == "charge reset") {
    resetChargeCaptures();
    testBattery();
    showTestDetail(
        "CHARGE PHASES",
        "Cleared. UP=battery, DOWN=charging, POWER=full.",
        {Battery}, &DiagnosticApp::runPowerTest, true);
  } else if (lower == "run rtc") {
    testRtc();
    showTestDetail("RTC RESULT", "PCF8563 result remains visible until Back.",
                   {RtcClock}, nullptr);
  } else if (lower == "run wifi") {
    runWifiTest();
  } else if (lower == "run ble") {
    runBleTest();
  } else if (lower == "run i2c") {
    testI2c();
    showTestDetail("I2C RESULT", "ACK scan remains visible; unknown addresses are not written.",
                   {I2cSensors, Frontlight}, &DiagnosticApp::runBusSensorTest);
  } else if (lower == "run memory") {
    testMemory();
    showTestDetail("MEMORY RESULT", "Flash/PSRAM sizes and pattern result.",
                   {Memory}, &DiagnosticApp::runBusSensorTest);
  } else if (lower == "run frontlight") {
    testFrontlight();
    showTestDetail("FRONTLIGHT RESULT", "Optional population; NOT FITTED is valid.",
                   {Frontlight}, &DiagnosticApp::runBusSensorTest);
  } else if (lower.startsWith("wifi connect ")) {
    testWifiConnect(command.substring(13));
    showTestDetail("WI-FI RESULT", "Credentials were RAM-only and have been discarded.",
                   {Wifi}, &DiagnosticApp::runWifiTest);
  } else if (lower.startsWith("rtc set ")) {
    setRtcFromCommand(command.substring(8));
    showTestDetail("RTC RESULT", "PCF8563 time was written and read back.",
                   {RtcClock}, nullptr);
  } else if (lower.startsWith("frontlight ")) {
    int brightness = 0;
    int warm = 50;
    sscanf(command.substring(11).c_str(), "%d %d", &brightness, &warm);
    testFrontlight(static_cast<uint8_t>(std::clamp(brightness, 0, 100)),
                   static_cast<uint8_t>(std::clamp(warm, 0, 100)));
    showTestDetail("FRONTLIGHT RESULT", "Driver always disables the output after the test.",
                   {Frontlight}, &DiagnosticApp::runBusSensorTest);
  } else if (lower == "screen black") {
    showDisplayPhase(4);
  } else if (lower == "screen white") {
    showDisplayPhase(5);
  } else if (lower == "screen checker" || lower == "screen fast") {
    showDisplayPhase(2);
  } else if (lower == "screen full") {
    showDisplayPhase(1);
  } else if (lower.startsWith("sleep ")) {
    timedDeepSleep(static_cast<uint32_t>(command.substring(6).toInt()));
  } else if (lower == "reboot") {
    Serial.println("Rebooting...");
    Serial.flush();
    ESP.restart();
  } else {
    Serial.println("Unknown command. Type 'help'.");
  }
}

void DiagnosticApp::runMenuItem(const uint8_t index) {
  if (index >= MENU_COUNT) return;
  (this->*MENU[index].action)();
}

void DiagnosticApp::returnToMenu() {
  view_ = View::Menu;
  renderMenu(false);
}

void DiagnosticApp::runAll() {
  view_ = View::Menu;
  showMessage("RUNNING ALL", "Display, memory, battery, SD, RTC, I2C, optional frontlight, Wi-Fi and BLE will be tested. Touch and keys remain interactive tests.", true);
  Serial.println("[TEST] Starting full automated suite");
  testDisplay();
  testMemory();
  testBattery();
  testInputIdleLevels();
  testStorage();
  testFonts();
  testRtc();
  testI2c();
  testFrontlight();
  testWifiScan();
  testBleScan();
  testUsb();
  renderReportScreen();
  printStatus();
}

void DiagnosticApp::runOverview() {
  probeBasicHardware();
  testBattery();
  testRtc();
  testI2c();
  testMemory();
  renderReportScreen();
}

void DiagnosticApp::enterTouchTest() {
  view_ = View::Touch;
  touchedCells_ = 0;
  setResult(Touch, input_.hasTouch() ? TestState::Running : TestState::Fail,
            input_.hasTouch() ? "GSLX680 ready; waiting for 9 touches" : "GSLX680 did not initialize");
  renderTouchTest(true);
}

void DiagnosticApp::enterButtonTest() {
  view_ = View::Buttons;
  buttonMask_ = 0;
  frontShortSeen_ = false;
  frontLongSeen_ = false;
  setResult(Buttons, TestState::Running, "waiting for UP/DOWN/POWER and front key short/long");
  renderButtonTest(true);
}

void DiagnosticApp::runDisplayTest() {
  displayVisitedMask_ = 0;
  showDisplayPhase(0);
}

void DiagnosticApp::runFontTest() {
  showMessage(
      "FONT PACK CHECK",
      "Validating six MiSansA4 CPFONT files and required English/Chinese glyphs. The interactive samples will remain on screen until input.",
      false);
  testFonts();
  fontVisitedMask_ = 0;
  showFontPage(0);
}

void DiagnosticApp::runStorageRtcTest() {
  showMessage("STORAGE + RTC", "Writing and reading one temporary SD file, then reading PCF8563 wall-clock registers.", false);
  testStorage();
  testRtc();
  showTestDetail("STORAGE + RTC", "Results remain until Back; Power repeats both tests.",
                 {Storage, RtcClock}, &DiagnosticApp::runStorageRtcTest);
}

void DiagnosticApp::runPowerTest() {
  testBattery();
  showTestDetail(
      "BATTERY + CHARGE",
      "UP=battery, DOWN=charging, POWER=full; samples stay in RAM.",
      {Battery}, &DiagnosticApp::runPowerTest, true);
}

void DiagnosticApp::runWifiTest() {
  showMessage("WI-FI SCAN", "Scanning all 2.4 GHz channels. Credentials are not required or stored.", false);
  testWifiScan();
  showTestDetail("WI-FI SCAN", "Scan result remains until Back; Power scans again.",
                 {Wifi}, &DiagnosticApp::runWifiTest);
}

void DiagnosticApp::runBleTest() {
  showMessage("BLE SCAN", "Running a five-second passive Bluetooth Low Energy scan. ESP32-S3 does not support Bluetooth Classic.", false);
  testBleScan();
  showTestDetail("BLUETOOTH LE", "Passive scan result remains until Back; Power scans again.",
                 {Ble}, &DiagnosticApp::runBleTest);
}

void DiagnosticApp::runBusSensorTest() {
  showMessage("BUS + MEMORY", "Scanning the shared I2C bus, safely probing the optional frontlight, and verifying a 1 MiB PSRAM pattern.", false);
  testI2c();
  testFrontlight();
  testMemory();
  showTestDetail("BUS + MEMORY", "Safe probes and PSRAM result remain until Back.",
                 {I2cSensors, Frontlight, Memory}, &DiagnosticApp::runBusSensorTest);
}

void DiagnosticApp::probeBasicHardware() {
  setResult(Display, display_.framebufferReady() ? TestState::Pass : TestState::Fail,
            display_.framebufferReady() ? "UC8279C framebuffer allocated, 768x552" : "framebuffer unavailable");
  setResult(Fonts, sd_.ready() ? TestState::Unknown : TestState::Fail,
            sd_.ready()
                ? "MiSansA4 CPFONT pack not validated yet; run font test"
                : "SD card unavailable; /fonts/MiSansA4 cannot be read");
  setResult(Touch, input_.hasTouch() ? TestState::Warn : TestState::Fail,
            input_.hasTouch() ? "GSLX680 firmware loaded; interactive orientation test pending"
                              : "GSLX680 probe/firmware load failed");
  testInputIdleLevels();
  setResult(Storage, sd_.ready() ? TestState::Warn : TestState::Fail,
            sd_.ready() ? String("card mounted, ") + sd_.sdTotalBytes() / (1024ULL * 1024ULL) + " MiB"
                        : "card not mounted");
  setResult(RtcClock, rtc_.present() ? TestState::Warn : TestState::Fail,
            rtc_.present() ? "PCF8563 ACK; time validity not checked" : "PCF8563 did not ACK");
  setResult(Frontlight, frontlight_.present() ? TestState::Warn : TestState::NotFitted,
            frontlight_.present() ? "LM3630A detected; low-brightness test pending"
                                  : "optional LM3630A not populated on this unit");
  setResult(Wifi, TestState::Unknown, "radio scan not run");
  setResult(Ble, TestState::Unknown, "BLE scan not run; Bluetooth Classic unsupported by ESP32-S3");
  setResult(I2cSensors, TestState::Unknown, "shared bus scan not run");
  setResult(Memory, TestState::Unknown, "flash/PSRAM test not run");
  testUsb();
}

void DiagnosticApp::testDisplay() {
  setResult(Display, TestState::Running, "full and fast waveform tests running");
  renderDisplayPattern(0);
  const uint32_t fullStart = millis();
  refresh(EInkDisplay::FULL_REFRESH);
  const uint32_t fullMs = millis() - fullStart;
  delay(350);
  renderDisplayPattern(1);
  const uint32_t fastStart = millis();
  refresh(EInkDisplay::FAST_REFRESH);
  const uint32_t fastMs = millis() - fastStart;
  setResult(Display, TestState::Pass,
            String("full=") + fullMs + "ms fast=" + fastMs + "ms; inspect borders/checker visually");
}

void DiagnosticApp::testFonts() {
  static constexpr uint8_t sizes[] = {8, 10, 12, 14, 16, 18};
  static constexpr const char* requiredEnglish =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  static constexpr const char* requiredChinese =
      "中文显示正常你好世界电子纸阅读器字体测试";

  setResult(Fonts, TestState::Running,
            "checking MiSansA4 CPFONT v4 files and English/Chinese coverage");
  fontPackReady_ = true;
  uint8_t loadedCount = 0;
  uint32_t totalMissing = 0;
  uint32_t totalIoErrors = 0;
  uint32_t referenceGlyphs = 0;
  uint32_t referenceIntervals = 0;

  for (const uint8_t pointSize : sizes) {
    char path[64] = {};
    snprintf(path, sizeof(path), "/fonts/MiSansA4/MiSansA4_%u.cpfont",
             pointSize);
    if (!fontRenderer_.load(path)) {
      fontPackReady_ = false;
      Serial.printf("[FONT] size=%upt load=FAIL reason=%s\n", pointSize,
                    fontRenderer_.error().c_str());
      continue;
    }
    ++loadedCount;
    if (referenceGlyphs == 0) {
      referenceGlyphs = fontRenderer_.info().glyphCount;
      referenceIntervals = fontRenderer_.info().intervalCount;
    }

    const uint16_t missingEnglish =
        fontRenderer_.countMissing(requiredEnglish);
    const uint16_t missingChinese =
        fontRenderer_.countMissing(requiredChinese);
    totalMissing += missingEnglish + missingChinese;

    // Draw outside the logical viewport to force glyph metadata/bitmap reads
    // without changing the currently visible diagnostic screen.
    uint16_t renderMissing = 0;
    uint16_t ioErrors = 0;
    fontRenderer_.drawText(-2000, -2000, requiredEnglish, &renderMissing,
                           &ioErrors);
    totalMissing += renderMissing;
    totalIoErrors += ioErrors;
    fontRenderer_.drawText(-2000, -2000, requiredChinese, &renderMissing,
                           &ioErrors);
    totalMissing += renderMissing;
    totalIoErrors += ioErrors;

    Serial.printf(
        "[FONT] size=%upt coverage_en_missing=%u coverage_zh_missing=%u "
        "bitmap_io_errors=%u glyphs=%lu intervals=%lu\n",
        pointSize, missingEnglish, missingChinese, ioErrors,
        static_cast<unsigned long>(fontRenderer_.info().glyphCount),
        static_cast<unsigned long>(fontRenderer_.info().intervalCount));
    fontRenderer_.unload();
  }

  fontRenderer_.unload();
  fontPackReady_ =
      fontPackReady_ && loadedCount == std::size(sizes) &&
      totalMissing == 0 && totalIoErrors == 0;
  if (fontPackReady_) {
    setResult(
        Fonts, TestState::Pass,
        String("MiSansA4 sizes=6/6; English+Chinese missing=0; bitmap I/O=0; glyphs=") +
            referenceGlyphs + " intervals=" + referenceIntervals);
  } else {
    setResult(
        Fonts, TestState::Fail,
        String("MiSansA4 sizes=") + loadedCount + "/6; missing=" +
            totalMissing + "; bitmap_io_errors=" + totalIoErrors +
            "; expected SD path=/fonts/MiSansA4");
  }
}

void DiagnosticApp::testGrayscale() {
  const size_t bytes = display_.getBufferSize();
  uint8_t* lsb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  uint8_t* msb = static_cast<uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!lsb || !msb) {
    if (lsb) heap_caps_free(lsb);
    if (msb) heap_caps_free(msb);
    setResult(Display, TestState::Fail, "could not allocate two grayscale planes");
    return;
  }
  memset(lsb, 0xff, bytes);
  memset(msb, 0xff, bytes);

  // Exercise every possible 2-bit plane combination in four equal native-X
  // bands. This is intentionally labelled by bit code in serial because panel
  // polarity/order is part of the visual test.
  for (uint16_t y = 0; y < eego::PANEL_HEIGHT; ++y) {
    for (uint16_t x = 0; x < eego::PANEL_WIDTH; ++x) {
      const uint8_t code = static_cast<uint8_t>(x * 4U / eego::PANEL_WIDTH);
      const uint32_t offset = static_cast<uint32_t>(y) * eego::PANEL_ROW_BYTES + x / 8;
      const uint8_t mask = static_cast<uint8_t>(0x80U >> (x & 7U));
      if ((code & 0x01U) == 0) lsb[offset] &= static_cast<uint8_t>(~mask);
      if ((code & 0x02U) == 0) msb[offset] &= static_cast<uint8_t>(~mask);
    }
  }
  display_.copyGrayscaleBuffers(lsb, msb);
  heap_caps_free(lsb);
  heap_caps_free(msb);

  Serial.println("[DISPLAY] grayscale bands, native left-to-right: 00 01 10 11");
  const uint32_t started = millis();
  display_.displayGrayBuffer(false);
  const uint32_t elapsed = millis() - started;
  display_.cleanupGrayscaleBuffers(display_.getFrameBuffer());
  setResult(Display, TestState::Pass,
            results_[Display].detail + String("; 4-level plane test=") + elapsed + "ms");
}

void DiagnosticApp::testStorage() {
  if (!sd_.ready()) {
    setResult(Storage, TestState::Fail, "microSD did not mount on SPI 39/40/38 CS47");
    return;
  }
  constexpr const char* path = "/eego-a4-diagnostics.tmp";
  const String expected = String("EEGO-A4-DIAG:") + millis() + ":0123456789abcdef";
  const bool wrote = sd_.writeFile(path, expected);
  const String actual = wrote ? sd_.readFile(path) : String();
  const bool removed = sd_.remove(path);
  if (!wrote || actual != expected || !removed) {
    setResult(Storage, TestState::Fail,
              String("temporary file write/read/remove: ") + wrote + "/" + (actual == expected) + "/" + removed);
    return;
  }
  setResult(Storage, TestState::Pass,
            String("R/W/verify/remove OK; capacity=") + sd_.sdTotalBytes() / (1024ULL * 1024ULL) + " MiB");
}

void DiagnosticApp::testBattery() {
  const BatteryMonitor::Status status = battery_.readStatus();
  if (!status.supported || !status.millivoltsKnown) {
    setResult(Battery, TestState::Fail, "ADC GPIO10 did not yield a valid battery voltage");
    return;
  }
  const int raw = analogRead(eego::BATTERY_ADC);
  const int chargeLevel = digitalRead(eego::CHARGE_STATUS);
  const bool frontlightPresent = frontlight_.present();
  const bool chargeAsserted =
      eego::chargeStatusAsserted(chargeLevel, frontlightPresent);
  const bool capturesComplete = chargeCapturesComplete();
  const bool capturePatternValid =
      capturesComplete && chargeCapturePatternValid();
  String detail = String(status.millivolts) + "mV";
  if (status.percentageKnown) detail += String(" ") + status.percentage + "%";
  detail += String(" ADCraw=") + raw;
  detail += String(" CHG_GPIO11=") + chargeLevel;
  detail += frontlightPresent ? " polarity=HIGH(frontlight)"
                              : " polarity=LOW(no-frontlight)";
  detail += chargeAsserted ? " signal=asserted" : " signal=inactive";
  detail += capturePatternValid
                ? " phases=VALIDATED"
                : (capturesComplete ? " phases=MISMATCH" : " phases=PENDING");
  detail += " captures=";
  detail += chargeCaptures_[static_cast<size_t>(ChargePhase::BatteryOnly)]
                    .captured
                ? "B"
                : "-";
  detail +=
      chargeCaptures_[static_cast<size_t>(ChargePhase::Charging)].captured
          ? "C"
          : "-";
  detail += chargeCaptures_[static_cast<size_t>(ChargePhase::Full)].captured
                ? "F"
                : "-";
  detail += String(" latch4=") + digitalRead(eego::POWER_LATCH);
  detail += String(" epdRail6=") + digitalRead(eego::EPD_POWER_ENABLE);
  setResult(Battery,
            capturePatternValid
                ? TestState::Pass
                : (capturesComplete ? TestState::Fail : TestState::Warn),
            detail);
}

void DiagnosticApp::testInputIdleLevels() {
  const int up = digitalRead(eego::BUTTON_UP);
  const int down = digitalRead(eego::BUTTON_DOWN);
  const int power = digitalRead(eego::BUTTON_POWER);
  const bool idleLevelsOk = up == HIGH && down == HIGH && power == LOW;
  const bool interactivePassed = results_[Buttons].state == TestState::Pass;
  String detail = String("idle GPIO5/7/8=") + up + "/" + down + "/" + power;
  if (!idleLevelsOk) {
    detail += "; unexpected level or key held";
    setResult(Buttons, TestState::Fail, detail);
    return;
  }
  detail += "; electrical idle OK";
  if (interactivePassed) {
    detail += "; prior interactive events passed";
    setResult(Buttons, TestState::Pass, detail);
  } else {
    detail += "; press events/front key still manual";
    setResult(Buttons, TestState::Warn, detail);
  }
}

void DiagnosticApp::testRtc() {
  if (!rtc_.present()) {
    setResult(RtcClock, TestState::Fail, "PCF8563 not detected at I2C 0x51");
    return;
  }
  Rtc::DateTime now{};
  if (!rtc_.now(now)) {
    setResult(RtcClock, TestState::Warn,
              "PCF8563 ACK but VL/oscillator-stop flag is set; use 'rtc set YYYY-MM-DDTHH:MM:SS'");
    return;
  }
  char value[32];
  snprintf(value, sizeof(value), "%04u-%02u-%02u %02u:%02u:%02u", now.year, now.month, now.day,
           now.hour, now.minute, now.second);
  setResult(RtcClock, TestState::Pass, value);
}

void DiagnosticApp::testWifiScan() {
  setResult(Wifi, TestState::Running, "2.4 GHz scan in progress");
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, false);
  delay(150);
  const int found = WiFi.scanNetworks(false, true);
  wifiSummary_ = "";
  if (found < 0) {
    setResult(Wifi, TestState::Fail, String("scanNetworks error=") + found);
  } else {
    const int shown = std::min(found, 6);
    for (int i = 0; i < shown; ++i) {
      if (i) wifiSummary_ += "; ";
      wifiSummary_ += WiFi.SSID(i) + "(" + WiFi.RSSI(i) + "dBm)";
      Serial.printf("[WIFI] %2d RSSI=%4d channel=%2d SSID=%s\n", i, WiFi.RSSI(i), WiFi.channel(i),
                    WiFi.SSID(i).c_str());
    }
    setResult(Wifi, TestState::Pass,
              String("scan completed, networks=") + found + (found ? String("; ") + wifiSummary_ : ""));
  }
  WiFi.scanDelete();
  WiFi.mode(WIFI_OFF);
}

void DiagnosticApp::testWifiConnect(const String& credentials) {
  const int separator = credentials.indexOf('|');
  if (separator <= 0) {
    setResult(Wifi, TestState::Fail, "usage: wifi connect SSID|PASSWORD");
    return;
  }
  const String ssid = credentials.substring(0, separator);
  const String password = credentials.substring(separator + 1);
  showMessage("WI-FI CONNECT", String("Connecting to ") + ssid + " for a non-persistent station test.", false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  const uint32_t deadline = millis() + 15'000;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) delay(100);
  if (WiFi.status() == WL_CONNECTED) {
    setResult(Wifi, TestState::Pass,
              String("connected ") + ssid + " IP=" + WiFi.localIP().toString() + " RSSI=" + WiFi.RSSI());
  } else {
    setResult(Wifi, TestState::Fail, String("connection timeout/status=") + WiFi.status());
  }
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
}

void DiagnosticApp::testBleScan() {
  setResult(Ble, TestState::Running, "five-second passive BLE scan");
  bleSummary_ = "";
  NimBLEDevice::init("EEGO-A4-DIAG");
  NimBLEScan* scan = NimBLEDevice::getScan();
  scan->setActiveScan(false);
  scan->setInterval(80);
  scan->setWindow(40);
  NimBLEScanResults found = scan->getResults(5000);
  const int count = found.getCount();
  const int shown = std::min(count, 8);
  for (int i = 0; i < shown; ++i) {
    const NimBLEAdvertisedDevice* device = found.getDevice(i);
    if (!device) continue;
    const String address(device->getAddress().toString().c_str());
    if (!bleSummary_.isEmpty()) bleSummary_ += "; ";
    bleSummary_ += address + "(" + device->getRSSI() + "dBm)";
    Serial.printf("[BLE] %2d RSSI=%4d address=%s\n", i, device->getRSSI(), address.c_str());
  }
  scan->clearResults();
  NimBLEDevice::deinit(true);
  setResult(Ble, TestState::Pass,
            String("passive scan completed, advertisers=") + count +
                (count ? String("; ") + bleSummary_ : String("; zero nearby is valid")));
}

void DiagnosticApp::testI2c() {
  setResult(I2cSensors, TestState::Running, "shared SDA2/SCL1 scan");
  Wire.begin(eego::I2C_SDA, eego::I2C_SCL, eego::I2C_HZ);
  Wire.setTimeOut(50);
  bool found[128] = {};
  i2cAddresses_ = "";
  for (uint8_t address = 1; address < 0x78; ++address) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      found[address] = true;
      char hex[6];
      snprintf(hex, sizeof(hex), "0x%02X", address);
      if (!i2cAddresses_.isEmpty()) i2cAddresses_ += ",";
      i2cAddresses_ += hex;
    }
  }
  const bool coreDevices = found[eego::GSLX680_ADDRESS] && found[eego::PCF8563_ADDRESS];
  String detail = String("ACK=") + (i2cAddresses_.isEmpty() ? "none" : i2cAddresses_);
  if (found[0x44]) detail += "; candidate SHT4x@0x44";
  if (found[0x6a] || found[0x6b]) detail += "; candidate IMU@0x6A/0x6B";
  if (!found[0x44] && !found[0x6a] && !found[0x6b]) detail += "; no IMU/environment sensor detected";
  setResult(I2cSensors, coreDevices ? TestState::Pass : TestState::Warn, detail);
  setResult(Frontlight, found[eego::LM3630A_ADDRESS] ? TestState::Warn : TestState::NotFitted,
            found[eego::LM3630A_ADDRESS] ? "LM3630A ACK at 0x36; illumination test pending"
                                        : "LM3630A path exists in firmware but no device ACK");
}

void DiagnosticApp::testFrontlight(const uint8_t brightness, const uint8_t warm) {
  if (!frontlight_.present()) {
    setResult(Frontlight, TestState::NotFitted, "optional LM3630A/frontlight not populated; no GPIO driven");
    return;
  }
  frontlight_.setColorTemperature(warm);
  frontlight_.setBrightness(brightness);
  delay(brightness == 0 ? 50 : 1200);
  frontlight_.off();
  setResult(Frontlight, TestState::Warn,
            String("LM3630A driven at ") + brightness + "% warm=" + warm +
                "% then disabled; visually confirm illumination");
}

void DiagnosticApp::testMemory() {
  setResult(Memory, TestState::Running, "checking flash size and 1 MiB PSRAM pattern");
  const uint32_t flash = ESP.getFlashChipSize();
  const uint32_t psram = ESP.getPsramSize();
  constexpr size_t testBytes = 1024U * 1024U;
  uint8_t* memory = static_cast<uint8_t*>(heap_caps_malloc(testBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  bool valid = memory != nullptr;
  if (memory) {
    for (size_t i = 0; i < testBytes; ++i) memory[i] = static_cast<uint8_t>((i * 131U + 0x5aU) & 0xffU);
    for (size_t i = 0; i < testBytes; ++i) {
      if (memory[i] != static_cast<uint8_t>((i * 131U + 0x5aU) & 0xffU)) {
        valid = false;
        break;
      }
    }
    heap_caps_free(memory);
  }
  const bool sizesOk = flash == eego::FLASH_BYTES && psram >= eego::PSRAM_BYTES;
  setResult(Memory, valid && sizesOk ? TestState::Pass : TestState::Fail,
            String("flash=") + flash / (1024U * 1024U) + "MiB PSRAM=" + psram / (1024U * 1024U) +
                "MiB pattern=" + (valid ? "OK" : "FAIL") + " free=" + ESP.getFreeHeap() / 1024U + "KiB");
}

void DiagnosticApp::testUsb() {
  if (Serial) {
    setResult(Usb, TestState::Pass, "native USB CDC enumerated; serial console is connected");
  } else {
    setResult(Usb, TestState::Warn,
              "native USB CDC started; open the 115200-baud console and send 'status' to prove RX/TX");
  }
}

void DiagnosticApp::setRtcFromCommand(const String& value) {
  unsigned year = 0;
  unsigned month = 0;
  unsigned day = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  if (sscanf(value.c_str(), "%u-%u-%uT%u:%u:%u", &year, &month, &day, &hour, &minute, &second) != 6 ||
      year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || day > 31 ||
      hour > 23 || minute > 59 || second > 59) {
    setResult(RtcClock, TestState::Fail, "usage: rtc set YYYY-MM-DDTHH:MM:SS");
    return;
  }
  const Rtc::DateTime dateTime{static_cast<uint16_t>(year), static_cast<uint8_t>(month),
                               static_cast<uint8_t>(day), static_cast<uint8_t>(hour),
                               static_cast<uint8_t>(minute), static_cast<uint8_t>(second), 0};
  if (!rtc_.set(dateTime)) {
    setResult(RtcClock, TestState::Fail, "PCF8563 write failed");
    return;
  }
  testRtc();
}

const char* DiagnosticApp::chargePhaseLabel(const ChargePhase phase) {
  switch (phase) {
    case ChargePhase::BatteryOnly: return "battery";
    case ChargePhase::Charging: return "charging";
    case ChargePhase::Full: return "full";
    case ChargePhase::Count: break;
  }
  return "unknown";
}

bool DiagnosticApp::chargeCapturesComplete() const {
  for (const ChargeCapture& capture : chargeCaptures_) {
    if (!capture.captured) return false;
  }
  return true;
}

bool DiagnosticApp::chargeCapturePatternValid() const {
  if (!chargeCapturesComplete()) return false;
  const bool frontlightPresent = frontlight_.present();
  const auto asserted = [frontlightPresent](const ChargeCapture& capture) {
    return eego::chargeStatusAsserted(capture.rawLevel, frontlightPresent);
  };
  return !asserted(
             chargeCaptures_[static_cast<size_t>(ChargePhase::BatteryOnly)]) &&
         asserted(chargeCaptures_[static_cast<size_t>(ChargePhase::Charging)]) &&
         !asserted(chargeCaptures_[static_cast<size_t>(ChargePhase::Full)]);
}

void DiagnosticApp::captureChargePhase(const String& phaseValue) {
  String phase = phaseValue;
  phase.trim();
  phase.toLowerCase();
  ChargePhase parsed = ChargePhase::Count;
  if (phase == "battery" || phase == "battery-only" || phase == "unplugged") {
    parsed = ChargePhase::BatteryOnly;
  } else if (phase == "charging" || phase == "charge" || phase == "usb") {
    parsed = ChargePhase::Charging;
  } else if (phase == "full" || phase == "charged") {
    parsed = ChargePhase::Full;
  }
  if (parsed == ChargePhase::Count) {
    Serial.println(
        "usage: charge capture battery|charging|full  (captures are RAM-only)");
    return;
  }

  const BatteryMonitor::Status status = battery_.readStatus();
  if (!status.supported || !status.millivoltsKnown) {
    Serial.printf("[CHARGE] phase=%s capture=FAIL reason=battery_adc\n",
                  chargePhaseLabel(parsed));
    setResult(Battery, TestState::Fail,
              "charge phase capture failed because battery ADC was invalid");
    return;
  }
  ChargeCapture& capture = chargeCaptures_[static_cast<size_t>(parsed)];
  capture.captured = true;
  capture.millivolts = status.millivolts;
  capture.rawLevel =
      static_cast<int8_t>(digitalRead(eego::CHARGE_STATUS));
  Serial.printf("[CHARGE] phase=%s captured=1 mv=%u gpio11=%d\n",
                chargePhaseLabel(parsed), capture.millivolts,
                capture.rawLevel);
  testBattery();
}

void DiagnosticApp::resetChargeCaptures() {
  for (ChargeCapture& capture : chargeCaptures_) capture = {};
  Serial.println("[CHARGE] RAM-only phase captures reset");
}

void DiagnosticApp::timedDeepSleep(uint32_t seconds) {
  seconds = std::clamp<uint32_t>(seconds, 3, 60);
  showMessage("TIMED DEEP SLEEP",
              String("Entering deep sleep for ") + seconds +
                  " seconds. GPIO4 remains latched high and the timer guarantees wake-up.",
              true);
  Serial.printf("[POWER] deep sleep for %lus\n", static_cast<unsigned long>(seconds));
  Serial.flush();
  eego::enterTimedDeepSleep(display_, frontlight_, seconds);
}

void DiagnosticApp::printHelp() const {
  Serial.println(
      "\nCommands:\n"
      "  help | status | ui state | report json | screenshot | menu | reboot\n"
      "  run all | run overview | run display | run fonts | run safe\n"
      "  run gray | run touch | run buttons\n"
      "  run sd | battery | rtc | wifi | ble | i2c | frontlight | memory\n"
      "  font page 1|2|3              (persistent CPFONT sample page)\n"
      "  screen safe | full | fast | gray | checker | black | white\n"
      "  wifi connect SSID|PASSWORD     (RAM only; not persisted)\n"
      "  rtc set YYYY-MM-DDTHH:MM:SS\n"
      "  charge capture battery|charging|full  (RAM-only phase proof)\n"
      "  charge reset\n"
      "  frontlight BRIGHTNESS [WARM]   (0..100; only if LM3630A ACKs)\n"
      "  sleep SECONDS                  (safe timer wake, clamped to 3..60)\n");
}

void DiagnosticApp::printStatus() const {
  Serial.println("\n=== EEGO A4 diagnostic status ===");
  for (const auto& result : results_) {
    Serial.printf("%-18s %-10s %s\n", result.name, stateLabel(result.state), result.detail.c_str());
  }
  Serial.printf("I2C: %s\nWi-Fi: %s\nBLE: %s\n", i2cAddresses_.c_str(), wifiSummary_.c_str(),
                bleSummary_.c_str());
}

void DiagnosticApp::printUiState() const {
  const char* label = "UNKNOWN";
  switch (view_) {
    case View::Menu: label = "MENU"; break;
    case View::Report: label = "REPORT"; break;
    case View::Detail: label = "DETAIL"; break;
    case View::Display: label = "DISPLAY"; break;
    case View::Fonts: label = "FONTS"; break;
    case View::Touch: label = "TOUCH"; break;
    case View::Buttons: label = "BUTTONS"; break;
  }
  Serial.printf("[UI] state=%s waiting_for_input=1", label);
  if (view_ == View::Detail) Serial.printf(" title=%s", detailTitle_.c_str());
  if (view_ == View::Display) {
    Serial.printf(" stage=%u/%u", static_cast<unsigned>(displayPhase_ + 1),
                  static_cast<unsigned>(DISPLAY_PHASE_COUNT));
  }
  if (view_ == View::Fonts) {
    Serial.printf(" page=%u/%u ready=%u visited=0x%02x",
                  static_cast<unsigned>(fontPage_ + 1),
                  static_cast<unsigned>(FONT_PAGE_COUNT), fontPackReady_,
                  fontVisitedMask_);
  }
  Serial.println();
}

String DiagnosticApp::jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value[i];
    if (ch == '"' || ch == '\\') {
      escaped += '\\';
      escaped += ch;
    } else if (ch == '\n') {
      escaped += "\\n";
    } else if (static_cast<uint8_t>(ch) >= 0x20U) {
      escaped += ch;
    }
  }
  return escaped;
}

void DiagnosticApp::printJsonReport() const {
  const int upLevel = digitalRead(eego::BUTTON_UP);
  const int downLevel = digitalRead(eego::BUTTON_DOWN);
  const int powerLevel = digitalRead(eego::BUTTON_POWER);
  const int chargeLevel = digitalRead(eego::CHARGE_STATUS);
  const bool frontlightPresent = frontlight_.present();
  const bool chargeValidated = chargeCapturePatternValid();
  Serial.print("{\"schema\":2,\"template\":\"");
  Serial.print(eego::TEMPLATE_VERSION);
  Serial.print("\",\"board\":\"EEGO A4\",\"mcu\":\"");
  Serial.print(ESP.getChipModel());
  Serial.printf("\",\"flash_bytes\":%lu,\"psram_bytes\":%lu,\"free_heap\":%lu,\"tests\":[",
                static_cast<unsigned long>(ESP.getFlashChipSize()),
                static_cast<unsigned long>(ESP.getPsramSize()),
                static_cast<unsigned long>(ESP.getFreeHeap()));
  for (uint8_t i = 0; i < TestCount; ++i) {
    if (i) Serial.print(',');
    Serial.printf("{\"name\":\"%s\",\"state\":\"%s\",\"detail\":\"%s\"}", results_[i].name,
                  stateLabel(results_[i].state), jsonEscape(results_[i].detail).c_str());
  }
  Serial.print(
      "],\"hardware_contract\":{\"display\":{\"controller\":\"UC8279C\","
      "\"framebuffer\":[768,552],\"controller_scan\":[768,600],"
      "\"spi\":{\"sclk\":42,\"mosi\":45,\"cs\":21,\"dc\":14,\"reset\":13,"
      "\"busy\":41,\"hz\":20000000},\"rail_gpio\":6},"
      "\"touch\":{\"controller\":\"GSLX680\",\"i2c_address\":64,"
      "\"sda\":2,\"scl\":1,\"ready\":");
  Serial.print(input_.hasTouch() ? "true" : "false");
  Serial.print(
      ",\"raw_ranges\":{\"x\":[0,920],\"y\":[0,680]},"
      "\"native_ranges\":{\"x\":[0,767],\"y\":[0,551]},"
      "\"front_key_raw\":[928,4128]},"
      "\"buttons\":{\"up_gpio\":5,\"down_gpio\":7,\"power_gpio\":8,"
      "\"idle_levels\":{\"up\":");
  Serial.print(upLevel);
  Serial.print(",\"down\":");
  Serial.print(downLevel);
  Serial.print(",\"power\":");
  Serial.print(powerLevel);
  Serial.print("},\"idle_levels_ok\":");
  Serial.print(upLevel == HIGH && downLevel == HIGH && powerLevel == LOW
                   ? "true"
                   : "false");
  Serial.print(
      "},\"storage\":{\"spi\":{\"sclk\":39,\"miso\":40,\"mosi\":38,"
      "\"cs\":47,\"hz\":20000000}},"
      "\"rtc\":{\"controller\":\"PCF8563\",\"i2c_address\":81},"
      "\"battery\":{\"adc_gpio\":10,\"divider\":1.559,"
      "\"charge_gpio\":11,\"charge_raw\":");
  Serial.print(chargeLevel);
  Serial.print(",\"frontlight_present\":");
  Serial.print(frontlightPresent ? "true" : "false");
  Serial.print(",\"charge_active_high\":");
  Serial.print(eego::chargeStatusActiveHigh(frontlightPresent) ? "true"
                                                               : "false");
  Serial.print(",\"charge_signal_asserted\":");
  Serial.print(eego::chargeStatusAsserted(chargeLevel, frontlightPresent)
                   ? "true"
                   : "false");
  Serial.print(
      ",\"polarity_evidence\":\"CrossLink 1.0.10 runtime LM3630A population "
      "branch\",\"phase_captures\":[");
  for (size_t i = 0; i < chargeCaptures_.size(); ++i) {
    if (i) Serial.print(',');
    const ChargeCapture& capture = chargeCaptures_[i];
    Serial.printf(
        "{\"phase\":\"%s\",\"captured\":%s,\"millivolts\":%u,"
        "\"raw\":%d}",
        chargePhaseLabel(static_cast<ChargePhase>(i)),
        capture.captured ? "true" : "false", capture.millivolts,
        capture.rawLevel);
  }
  Serial.print("],\"three_phase_validated\":");
  Serial.print(chargeValidated ? "true" : "false");
  Serial.print(
      "},\"power\":{\"latch_gpio\":4,\"display_rail_gpio\":6},"
      "\"optional_frontlight\":{\"controller\":\"LM3630A\","
      "\"i2c_address\":54,\"enable_gpio\":12,\"present\":");
  Serial.print(frontlightPresent ? "true" : "false");
  Serial.print(
      "}},\"manual_required\":{\"touch_grid\":");
  Serial.print(results_[Touch].state == TestState::Pass ? "false" : "true");
  Serial.print(",\"button_events\":");
  Serial.print(results_[Buttons].state == TestState::Pass ? "false" : "true");
  Serial.print(",\"display_visual_quality\":");
  const uint8_t allDisplayPhases =
      static_cast<uint8_t>((1U << DISPLAY_PHASE_COUNT) - 1U);
  Serial.print(displayVisitedMask_ == allDisplayPhases ? "false" : "true");
  Serial.print(",\"charge_three_phase\":");
  Serial.print(chargeValidated ? "false" : "true");
  Serial.print(
      ",\"deep_sleep_current\":true},"
      "\"declared_absent\":{\"bluetooth_classic\":true,\"audio\":true,"
      "\"microphone\":true,\"led\":true},"
      "\"notes\":\"IMU and environment sensors are safe-probed but not "
      "declared present; charge captures are RAM-only.\"}\n");
}
