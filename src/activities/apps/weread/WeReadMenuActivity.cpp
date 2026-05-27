#include "WeReadMenuActivity.h"

#include <I18n.h>
#include <WiFi.h>

#include <memory>
#include <string>

#include "../../../components/UITheme.h"
#include "../../ActivityManager.h"
#include "../../network/WifiSelectionActivity.h"
#include "WeReadKeyStore.h"
#include "WeReadSetupActivity.h"
#include "WeReadSyncAllActivity.h"

namespace {

// Menu items in display order. Action stubs return to the menu with a
// "coming soon" banner until the corresponding feature Activity lands.
enum class MenuItem : uint8_t {
  Shelf = 0,
  Search,
  ForYou,
  Stats,
  SyncAll,
  Setup,
};

constexpr int kMenuItemCount = 6;

StrId titleFor(int idx) {
  switch (static_cast<MenuItem>(idx)) {
    case MenuItem::Shelf:
      return StrId::STR_WEREAD_MENU_SHELF;
    case MenuItem::Search:
      return StrId::STR_WEREAD_MENU_SEARCH;
    case MenuItem::ForYou:
      return StrId::STR_WEREAD_MENU_RECOMMEND;
    case MenuItem::Stats:
      return StrId::STR_WEREAD_MENU_STATS;
    case MenuItem::SyncAll:
      return StrId::STR_WEREAD_MENU_SYNC_ALL;
    case MenuItem::Setup:
      return StrId::STR_WEREAD_MENU_SETUP;
  }
  return StrId::STR_WEREAD_MENU_SHELF;
}

StrId subtitleFor(int idx, bool keyOk) {
  if (static_cast<MenuItem>(idx) == MenuItem::Setup) {
    return keyOk ? StrId::STR_WEREAD_KEY_SET : StrId::STR_WEREAD_KEY_NOT_SET;
  }
  // Default subtitle for other rows; rendering layer falls back to empty if
  // the key resolves to an empty translation.
  return StrId::STR_WEREAD_NONE;
}

}  // namespace

WeReadMenuActivity::WeReadMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("WeReadMenu", renderer, mappedInput) {}

void WeReadMenuActivity::onEnter() {
  Activity::onEnter();
  selected = 0;
  banner = Banner::None;
  pendingItemAfterConnect_ = -1;
  refreshGates();  // Only read state; WiFi stays off until the user picks a
                   // network-requiring item — see onSelect().
  requestUpdate();
}

void WeReadMenuActivity::launchAutoConnect() {
  auto handler = [this](const ActivityResult&) {
    // WiFi.status() (via refreshGates) is the authoritative source — the
    // sub-activity's WifiResult.connected flag would work too but we'd still
    // need refreshGates() here so FetchActivity preflights see the same view.
    refreshGates();

    if (pendingItemAfterConnect_ >= 0) {
      const int target = pendingItemAfterConnect_;
      pendingItemAfterConnect_ = -1;
      if (wifiOk) {
        // Auto-connect succeeded — continue into the sub-activity the user
        // originally picked, so the press-then-wait flow feels like one act.
        // keyOk was already verified by onSelect before scheduling pending.
        dispatchMenuItem(target);
        return;
      }
      banner = Banner::NoWifi;  // Connect failed/cancelled; surface it on the menu.
    }
    requestUpdate();
  };
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, /*autoConnect=*/true), handler);
}

void WeReadMenuActivity::onExit() { Activity::onExit(); }

void WeReadMenuActivity::refreshGates() {
  wifiOk = (WiFi.status() == WL_CONNECTED);
  keyOk = WeReadKeyStore::has();
}

void WeReadMenuActivity::loop() {
  buttonNavigator.onNext([this] {
    selected = ButtonNavigator::nextIndex(selected, kMenuItemCount);
    banner = Banner::None;
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selected = ButtonNavigator::previousIndex(selected, kMenuItemCount);
    banner = Banner::None;
    requestUpdate();
  });

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    onSelect();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void WeReadMenuActivity::onSelect() {
  const auto item = static_cast<MenuItem>(selected);

  if (item == MenuItem::Setup) {
    auto handler = [this](const ActivityResult&) {
      // Setup Activity returns here regardless of save/cancel — re-check gates.
      refreshGates();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<WeReadSetupActivity>(renderer, mappedInput), handler);
    return;
  }

  // All non-Setup actions need both an API key and Wi-Fi.
  refreshGates();
  if (!keyOk) {
    // No key → no point spinning up Wi-Fi for a request that would fail. Hand
    // the user to Setup; they'll come back here to pick again afterwards.
    auto handler = [this](const ActivityResult&) {
      refreshGates();
      requestUpdate();
    };
    startActivityForResult(std::make_unique<WeReadSetupActivity>(renderer, mappedInput), handler);
    return;
  }
  if (!wifiOk) {
    // Lazily connect: remember which item the user wanted, hand off to
    // WifiSelectionActivity, and resume dispatch in launchAutoConnect's
    // completion handler.
    pendingItemAfterConnect_ = selected;
    launchAutoConnect();
    return;
  }

  dispatchMenuItem(selected);
}

void WeReadMenuActivity::dispatchMenuItem(int itemIndex) {
  // Caller is responsible for ensuring wifiOk && keyOk; this just routes.
  switch (static_cast<MenuItem>(itemIndex)) {
    case MenuItem::Shelf:
      activityManager.goToWeReadShelf();
      break;
    case MenuItem::Search:
      activityManager.goToWeReadSearch();
      break;
    case MenuItem::ForYou:
      activityManager.goToWeReadRecommend();
      break;
    case MenuItem::Stats:
      activityManager.goToWeReadStats();
      break;
    case MenuItem::SyncAll: {
      auto handler = [this](const ActivityResult&) {
        refreshGates();
        requestUpdate();
      };
      startActivityForResult(std::make_unique<WeReadSyncAllActivity>(renderer, mappedInput), handler);
      break;
    }
    case MenuItem::Setup:
      break;  // Setup is handled upstream and never routed through here.
  }
}

void WeReadMenuActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_WEREAD_TITLE));

  const int listY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int listH = sh - listY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  const bool keyOkLocal = keyOk;
  GUI.drawList(
      renderer, Rect{0, listY, sw, listH}, kMenuItemCount, selected,
      [](int i) { return std::string(I18n::getInstance().get(titleFor(i))); },
      [keyOkLocal](int i) { return std::string(I18n::getInstance().get(subtitleFor(i, keyOkLocal))); });

  // Banner pops over the menu — drawn last so it sits on top.
  if (banner == Banner::NoWifi) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_NO_WIFI));
  } else if (banner == Banner::ComingSoon) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_COMING_SOON));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
