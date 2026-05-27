#include "WeReadSetupActivity.h"

#include <I18n.h>

#include <cstdio>
#include <memory>
#include <string>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../ActivityManager.h"
#include "../../network/CrossPointWebServerActivity.h"
#include "../../util/KeyboardEntryActivity.h"
#include "WeReadKeyStore.h"

namespace {

constexpr int kIdxWeb = 0;
constexpr int kIdxManual = 1;
constexpr int kIdxClear = 2;  // only when keyPresent

}  // namespace

WeReadSetupActivity::WeReadSetupActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("WeReadSetup", renderer, mappedInput) {}

void WeReadSetupActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  banner = Banner::None;
  refresh();
  requestUpdate();
}

void WeReadSetupActivity::onExit() { Activity::onExit(); }

void WeReadSetupActivity::refresh() { keyPresent = WeReadKeyStore::has(); }

int WeReadSetupActivity::itemCount() const { return keyPresent ? 3 : 2; }

void WeReadSetupActivity::loop() {
  const int count = itemCount();
  buttonNavigator.onNext([this, count] {
    selected = ButtonNavigator::nextIndex(selected, count);
    banner = Banner::None;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count] {
    selected = ButtonNavigator::previousIndex(selected, count);
    banner = Banner::None;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelect();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
  }
}

void WeReadSetupActivity::launchKeyboardEntry() {
  auto handler = [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    const auto& kb = std::get<KeyboardResult>(result.data);
    if (!WeReadKeyStore::isWellFormed(kb.text)) {
      banner = Banner::Invalid;
      requestUpdate();
      return;
    }
    if (WeReadKeyStore::save(kb.text)) {
      banner = Banner::Saved;
      refresh();
      selected = 0;
    } else {
      banner = Banner::Invalid;
    }
    requestUpdate();
  };
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, std::string(tr(STR_WEREAD_API_KEY_PROMPT)),
                                              std::string(WeReadKeyStore::load()), 128, InputType::Text),
      handler);
}

void WeReadSetupActivity::launchWebServer() {
  // Remember the pre-launch state so we can detect "key was set via the web
  // form" when the WebServer activity finishes (user pressed Back).
  keyWasPresentOnWebEnter = WeReadKeyStore::has();
  auto handler = [this](const ActivityResult&) {
    refresh();
    if (keyPresent && !keyWasPresentOnWebEnter) {
      banner = Banner::Saved;
      selected = 0;
    }
    requestUpdate();
  };
  startActivityForResult(
      std::make_unique<CrossPointWebServerActivity>(renderer, mappedInput, WebServerEntryPurpose::WeReadKeySetup),
      handler);
}

void WeReadSetupActivity::onSelect() {
  if (selected == kIdxWeb) {
    launchWebServer();
  } else if (selected == kIdxManual) {
    launchKeyboardEntry();
  } else if (selected == kIdxClear && keyPresent) {
    WeReadKeyStore::clear();
    banner = Banner::Cleared;
    refresh();
    selected = 0;
    requestUpdate();
  }
}

void WeReadSetupActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_WEREAD_MENU_SETUP));

  // Guide paragraph: intro line + URL line (bold). Pulls users out of the
  // dead-end "I have no idea what to paste here" state by naming the source.
  const int contentWidth = sw - 2 * metrics.contentSidePadding;
  const int x = metrics.contentSidePadding;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);

  int y = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  for (const auto& line : renderer.wrappedText(UI_10_FONT_ID, tr(STR_WEREAD_SETUP_GUIDE_INTRO), contentWidth, 2)) {
    renderer.drawText(UI_10_FONT_ID, x, y, line.c_str());
    y += lineHeight;
  }
  renderer.drawText(UI_10_FONT_ID, x, y, tr(STR_WEREAD_SETUP_GUIDE_URL), true, EpdFontFamily::BOLD);
  y += lineHeight + metrics.verticalSpacing;

  const int listY = y;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const bool present = keyPresent;
  GUI.drawList(
      renderer, Rect{0, listY, sw, listH}, itemCount(), selected,
      [](int i) {
        if (i == kIdxWeb) return std::string(tr(STR_WEREAD_SETUP_VIA_WEB));
        if (i == kIdxManual) return std::string(tr(STR_WEREAD_SETUP_VIA_KEYBOARD));
        return std::string(tr(STR_WEREAD_KEY_CLEAR));
      },
      [present](int i) {
        if (i == kIdxWeb) return std::string(tr(STR_WEREAD_SETUP_VIA_WEB_SUB));
        if (i == kIdxManual) return std::string(tr(STR_WEREAD_SETUP_VIA_KEYBOARD_SUB));
        return std::string(
            I18n::getInstance().get(present ? StrId::STR_WEREAD_KEY_SET : StrId::STR_WEREAD_KEY_NOT_SET));
      });

  switch (banner) {
    case Banner::Saved:
      GUI.drawPopup(renderer, tr(STR_WEREAD_KEY_SAVED));
      break;
    case Banner::Cleared:
      GUI.drawPopup(renderer, tr(STR_WEREAD_KEY_WAS_CLEARED));
      break;
    case Banner::Invalid:
      GUI.drawPopup(renderer, tr(STR_WEREAD_API_KEY_INVALID));
      break;
    case Banner::None:
      break;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
