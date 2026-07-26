#include "WeReadActivity.h"

#include <Arduino.h>
#include <FontCacheManager.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "../../../CrossPointState.h"
#include "../../../SdCardFontSystem.h"
#include "../../../SilentRestart.h"
#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../../util/QrUtils.h"
#include "../../ActivityManager.h"
#include "../../network/WifiSelectionActivity.h"
#include "../../util/ConfirmationActivity.h"

namespace {

static_assert(sizeof(WeReadClient::Operation) <= 8 * 1024, "WeRead workspace exceeds its fixed heap budget");

enum class MenuAction : uint8_t { Shelf, Refresh, Logout };

struct MenuEntry {
  StrId title;
  MenuAction action;
};

constexpr MenuEntry kMenuEntries[] = {
    {StrId::STR_WEREAD_MENU_SHELF, MenuAction::Shelf},
    {StrId::STR_WEREAD_MENU_REFRESH, MenuAction::Refresh},
    {StrId::STR_WEREAD_MENU_LOGOUT, MenuAction::Logout},
};

constexpr int kMenuEntryCount = static_cast<int>(sizeof(kMenuEntries) / sizeof(kMenuEntries[0]));

void logHeap([[maybe_unused]] const char* phase) {
  LOG_DBG("WR", "%s: free=%u largest=%u stack=%u", phase, static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
}

}  // namespace

void WeReadActivity::onEnter() {
  Activity::onEnter();
  {
    RenderLock lock(*this);
    sdFontSystem.releaseLoadedFont(renderer);
    if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  }
  menuSelected_ = 0;
  shelfSelected_ = 0;
  // This bounded 832-byte probe is gone before TLS and avoids a transient heap
  // allocation that could fragment the ESP32-C3 heap.
  WeReadStore::Session session;
  const bool loggedIn = WeReadStore::loadSession(session);
  session.clear();
  if (loggedIn) {
    state_.store(State::Menu);
    requestUpdate();
  } else {
    syncShelf();
  }
}

void WeReadActivity::onExit() {
  operation_.reset();
  downloadRenderPending_.store(false);
  if (shelfFile_.isOpen()) shelfFile_.close();
  Activity::onExit();
}

bool WeReadActivity::refreshShelf() {
  if (shelfFile_.isOpen()) shelfFile_.close();
  shelfCount_ = 0;
  if (!WeReadStore::openShelf(shelfFile_, shelfCount_)) {
    if (shelfFile_.isOpen()) shelfFile_.close();
    return false;
  }
  if (shelfCount_ == 0) {
    shelfSelected_ = 0;
  } else if (shelfSelected_ >= static_cast<int>(shelfCount_)) {
    shelfSelected_ = static_cast<int>(shelfCount_ - 1);
  }
  return true;
}

bool WeReadActivity::readShelf(const int index, WeReadStore::ShelfRecord& record) const {
  return index >= 0 && static_cast<uint32_t>(index) < shelfCount_ &&
         WeReadStore::readShelfRecord(shelfFile_, static_cast<uint32_t>(index), record);
}

void WeReadActivity::requestDownloadUpdate() {
  if (!downloadRenderPending_.exchange(true)) requestUpdate();
}

void WeReadActivity::requestJobUpdate() {
  if (retryJob_ == Job::Download) {
    requestDownloadUpdate();
  } else {
    requestUpdate();
  }
}

void WeReadActivity::connectThen(const Job job, const WeReadStore::ShelfRecord* book) {
  retryJob_ = job;
  if (book) pendingBook_ = *book;
  auto wifi = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput, true);
  if (!wifi) {
    LOG_ERR("WR", "OOM: Wi-Fi activity");
    error_ = WeReadClient::Error::OutOfMemory;
    state_.store(State::Error);
    requestJobUpdate();
    return;
  }
  startActivityForResult(std::move(wifi), [this, job](const ActivityResult&) {
    if (WiFi.status() == WL_CONNECTED) {
      startJob(job, job == Job::Download ? &pendingBook_ : nullptr);
      return;
    }
    error_ = WeReadClient::Error::Network;
    state_.store(State::Error);
    requestJobUpdate();
  });
}

void WeReadActivity::startJob(const Job job, const WeReadStore::ShelfRecord* book) {
  if (job == Job::Sync && shelfFile_.isOpen()) shelfFile_.close();
  if (job == Job::Download && book) pendingBook_ = *book;
  retryJob_ = job;
  error_ = WeReadClient::Error::Ok;
  completedChapters_.store(0);
  chapterCount_.store(0);
  downloadRenderPending_.store(false);
  qrUrl_[0] = '\0';
  const auto kind = job == Job::Sync ? WeReadClient::Operation::Kind::Sync : WeReadClient::Operation::Kind::Download;
  if (!operation_.begin(kind, book)) {
    error_ = operation_.error();
    state_.store(State::Error);
    requestJobUpdate();
  } else {
    state_.store(job == Job::Sync ? State::Connecting : State::Downloading);
    if (job == Job::Sync) {
      requestUpdateAndWait();
    } else {
      requestDownloadUpdate();
    }
  }
}

void WeReadActivity::advanceJob() {
  // Rendering owns a second 8KB task and large display buffers. Serialize it
  // with each synchronous protocol step so TLS never competes with a refresh.
  RenderLock renderBarrier(*this);
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->clearCache();
  const auto event = operation_.step();
  if (retryJob_ == Job::Download) {
    const uint32_t completed = operation_.completedChapters();
    const uint32_t total = operation_.chapterCount();
    const bool completedChanged = completedChapters_.exchange(completed) != completed;
    const bool totalChanged = chapterCount_.exchange(total) != total;
    if (completedChanged || totalChanged) requestDownloadUpdate();
  }

  switch (event) {
    case WeReadClient::Operation::Event::None:
      return;
    case WeReadClient::Operation::Event::QrReady:
      strncpy(qrUrl_, operation_.qrUrl(), sizeof(qrUrl_) - 1);
      qrUrl_[sizeof(qrUrl_) - 1] = '\0';
      state_.store(State::Qr);
      requestJobUpdate();
      return;
    case WeReadClient::Operation::Event::Authenticated:
      state_.store(State::LoginConfirmed);
      return;
    case WeReadClient::Operation::Event::ChapterComplete:
      state_.store(State::Downloading);
      return;
    case WeReadClient::Operation::Event::Complete:
      if (retryJob_ == Job::Download) {
        state_.store(State::OpenBook);
        openBook(operation_.finalPath());
      } else {
        refreshShelf();
        state_.store(State::Shelf);
        requestJobUpdate();
      }
      return;
    case WeReadClient::Operation::Event::Cancelled:
      refreshShelf();
      state_.store(retryJob_ == Job::Download ? State::Shelf : State::Menu);
      requestJobUpdate();
      return;
    case WeReadClient::Operation::Event::Failed:
      error_ = operation_.error();
      refreshShelf();
      state_.store(State::Error);
      requestJobUpdate();
      return;
  }
}

void WeReadActivity::activateSelected() {
  WeReadStore::ShelfRecord book;
  if (!readShelf(shelfSelected_, book)) return;
  const std::string path = WeReadStore::finalBookPath(book);
  if (Storage.exists(path.c_str())) {
    openBook(path.c_str());
    return;
  }
  if (WiFi.status() == WL_CONNECTED) {
    startJob(Job::Download, &book);
  } else {
    connectThen(Job::Download, &book);
  }
}

void WeReadActivity::openBook(const char* path) {
  logHeap("open reader");
#ifdef CROSSPOINT_EMULATED
  activityManager.goToReader(path);
#else
  if (WiFi.getMode() == WIFI_MODE_NULL) {
    activityManager.goToReader(path);
    return;
  }

  APP_STATE.openEpubPath = path;
  APP_STATE.readerActivityLoadCount = 0;
  if (!APP_STATE.saveToFile()) {
    LOG_ERR("WR", "Failed to persist reader target; opening without restart");
    activityManager.goToReader(path);
    return;
  }

  WiFi.disconnect(false);
  delay(30);
  silentRestartToReader();
#endif
}

void WeReadActivity::openShelf() {
  if (refreshShelf()) {
    state_.store(State::Shelf);
    requestUpdate();
    return;
  }
  syncShelf();
}

void WeReadActivity::syncShelf() {
  if (WiFi.status() == WL_CONNECTED) {
    startJob(Job::Sync);
  } else {
    connectThen(Job::Sync);
  }
}

void WeReadActivity::promptLogout() {
  // ActivityManager owns the confirmation across frames, so this must be a
  // fallible heap allocation rather than a stack object.
  auto confirmation = makeUniqueNoThrow<ConfirmationActivity>(renderer, mappedInput, tr(STR_WEREAD_LOGOUT_CONFIRM),
                                                              tr(STR_WEREAD_LOGOUT_KEEP_DOWNLOADS));
  if (!confirmation) {
    LOG_ERR("WR", "OOM: logout confirmation (%zu bytes)", sizeof(ConfirmationActivity));
    return;
  }
  startActivityForResult(std::move(confirmation), [this](const ActivityResult& result) {
    if (result.isCancelled) {
      requestUpdate();
      return;
    }
    performLogout();
  });
}

void WeReadActivity::performLogout() {
  operation_.reset();
  if (shelfFile_.isOpen()) shelfFile_.close();
  const bool sessionCleared = WeReadStore::clearSession();
  const bool shelfCleared = WeReadStore::clearShelf();
  shelfCount_ = 0;
  shelfSelected_ = 0;
  if (!sessionCleared || !shelfCleared) {
    LOG_ERR("WR", "Failed to clear local login state");
    state_.store(State::LogoutError);
    requestUpdate();
    return;
  }
  menuSelected_ = 0;
  state_.store(State::Menu);
  requestUpdate();
}

void WeReadActivity::handleMenuInput() {
  buttonNavigator_.onNext([this] {
    menuSelected_ = ButtonNavigator::nextIndex(menuSelected_, kMenuEntryCount);
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this] {
    menuSelected_ = ButtonNavigator::previousIndex(menuSelected_, kMenuEntryCount);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    switch (kMenuEntries[menuSelected_].action) {
      case MenuAction::Shelf:
        openShelf();
        return;
      case MenuAction::Refresh:
        syncShelf();
        return;
      case MenuAction::Logout:
        promptLogout();
        return;
    }
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
  }
}

void WeReadActivity::handleShelfInput() {
  const int count = static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX));
  buttonNavigator_.onNext([this, count] {
    shelfSelected_ = ButtonNavigator::nextIndex(shelfSelected_, count);
    requestUpdate();
  });
  buttonNavigator_.onPrevious([this, count] {
    shelfSelected_ = ButtonNavigator::previousIndex(shelfSelected_, count);
    requestUpdate();
  });
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    activateSelected();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(State::Menu);
    requestUpdate();
  }
}

void WeReadActivity::handleErrorInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (WiFi.status() == WL_CONNECTED) {
      if (retryJob_ == Job::Download) {
        WeReadStore::ShelfRecord book;
        if (readShelf(shelfSelected_, book)) startJob(Job::Download, &book);
      } else {
        startJob(Job::Sync);
      }
    } else {
      WeReadStore::ShelfRecord book;
      if (retryJob_ != Job::Download || readShelf(shelfSelected_, book)) {
        connectThen(retryJob_, retryJob_ == Job::Download ? &book : nullptr);
      }
    }
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    operation_.reset();
    state_.store(retryJob_ == Job::Download ? State::Shelf : State::Menu);
    requestJobUpdate();
  }
}

void WeReadActivity::handleLogoutErrorInput() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    performLogout();
  } else if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    state_.store(State::Menu);
    requestUpdate();
  }
}

void WeReadActivity::loop() {
  const State state = state_.load();
  switch (state) {
    case State::Menu:
      handleMenuInput();
      return;
    case State::Shelf:
      handleShelfInput();
      return;
    case State::Error:
      handleErrorInput();
      return;
    case State::LogoutError:
      handleLogoutErrorInput();
      return;
    case State::LoginConfirmed:
      requestUpdateAndWait();
      state_.store(retryJob_ == Job::Sync ? State::Syncing : State::Downloading);
      return;
    case State::OpenBook:
      return;
    case State::Connecting:
    case State::Qr:
    case State::Syncing:
    case State::Downloading:
    case State::Cancelling:
      break;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    operation_.cancel();
    state_.store(State::Cancelling);
    requestJobUpdate();
    return;
  }
  if (state == State::Downloading && completedChapters_.load() == 0 && chapterCount_.load() == 0 &&
      downloadRenderPending_.load()) {
    return;
  }
  advanceJob();
}

bool WeReadActivity::isBusy(const State state) {
  return state == State::Connecting || state == State::Qr || state == State::LoginConfirmed ||
         state == State::Syncing || state == State::Downloading || state == State::Cancelling;
}

const char* WeReadActivity::errorMessage() const {
  switch (error_) {
    case WeReadClient::Error::SdCard:
      return tr(STR_WEREAD_CACHE_FAILED);
    case WeReadClient::Error::Network:
      return WiFi.status() == WL_CONNECTED ? tr(STR_WEREAD_HTTP_ERROR) : tr(STR_WEREAD_NO_WIFI);
    case WeReadClient::Error::Unavailable:
      return tr(STR_WEREAD_CACHE_NOT_AVAILABLE);
    default:
      return tr(STR_WEREAD_HTTP_ERROR);
  }
}

void WeReadActivity::render(RenderLock&&) {
  downloadRenderPending_.store(false);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentH = height - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect content{0, contentY, width, contentH};
  const State state = state_.load();

  renderer.clearScreen();
  const char* header =
      state == State::Shelf || state == State::Downloading ? tr(STR_WEREAD_MENU_SHELF) : tr(STR_WEREAD_TITLE);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, header);

  switch (state) {
    case State::Menu:
      GUI.drawButtonMenu(
          renderer, content, kMenuEntryCount, menuSelected_,
          [](const int index) { return std::string(I18N.get(kMenuEntries[index].title)); }, nullptr);
      break;
    case State::Shelf:
      if (shelfCount_ == 0) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_SHELF_EMPTY));
      } else {
        GUI.drawList(
            renderer, content, static_cast<int>(std::min<uint32_t>(shelfCount_, INT32_MAX)), shelfSelected_,
            [this](const int index) {
              WeReadStore::ShelfRecord book;
              return readShelf(index, book) ? std::string(book.title) : std::string();
            },
            [this](const int index) {
              WeReadStore::ShelfRecord book;
              if (!readShelf(index, book)) return std::string();
              std::string subtitle(book.author);
              if (Storage.exists(WeReadStore::finalBookPath(book).c_str())) {
                if (!subtitle.empty()) subtitle += " · ";
                subtitle += tr(STR_WEREAD_CACHE_BADGE);
              }
              return subtitle;
            });
      }
      break;
    case State::Qr: {
      if (!qrUrl_[0]) {
        GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
        break;
      }
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int textGap = metrics.verticalSpacing;
      const int qrLimit = content.height - textGap - lineHeight * 2;
      const int qrSide = std::max(1, std::min(content.width * 4 / 5, qrLimit));
      const int groupHeight = qrSide + textGap + lineHeight * 2;
      const int qrY = content.y + std::max(0, (content.height - groupHeight) / 2);
      QrUtils::drawQrCode(renderer, Rect{(width - qrSide) / 2, qrY, qrSide, qrSide}, qrUrl_);
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap, tr(STR_WEREAD_SCAN_LOGIN));
      char target[64];
      snprintf(target, sizeof(target), "\"%s\"", tr(STR_WEREAD_TITLE));
      renderer.drawCenteredText(UI_10_FONT_ID, qrY + qrSide + textGap + lineHeight, target);
      break;
    }
    case State::Downloading: {
      const uint32_t completed = completedChapters_.load();
      const uint32_t total = chapterCount_.load();
      const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
      const int centerY = (height - lineHeight) / 2;
      char status[64];
      if (total == 0) {
        snprintf(status, sizeof(status), "%s", tr(STR_WEREAD_CACHING));
      } else {
        snprintf(status, sizeof(status), "%s %u/%u", tr(STR_WEREAD_CACHING), static_cast<unsigned>(completed),
                 static_cast<unsigned>(total));
      }
      renderer.drawCenteredText(UI_10_FONT_ID, centerY - lineHeight - metrics.verticalSpacing, pendingBook_.title);
      renderer.drawCenteredText(UI_10_FONT_ID, centerY, status);
      const int barY = centerY + lineHeight + metrics.verticalSpacing;
      GUI.drawProgressBar(
          renderer,
          Rect{metrics.contentSidePadding, barY, width - metrics.contentSidePadding * 2, metrics.progressBarHeight},
          completed, total == 0 ? 100 : total);
      break;
    }
    case State::Error:
      GUI.drawPopup(renderer, errorMessage());
      break;
    case State::LogoutError:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGOUT_FAILED));
      break;
    case State::LoginConfirmed:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOGIN_CONFIRMED));
      break;
    case State::Connecting:
    case State::Syncing:
    case State::Cancelling:
    case State::OpenBook:
      GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
      break;
  }

  const char* back = "";
  const char* confirm = "";
  const char* previous = "";
  const char* next = "";
  switch (state) {
    case State::Menu:
      back = tr(STR_BACK);
      confirm = tr(STR_SELECT);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case State::Shelf:
      back = tr(STR_BACK);
      confirm = tr(STR_OPEN);
      previous = tr(STR_DIR_UP);
      next = tr(STR_DIR_DOWN);
      break;
    case State::Error:
    case State::LogoutError:
      back = tr(STR_BACK);
      confirm = tr(STR_RETRY);
      break;
    case State::Connecting:
    case State::Qr:
    case State::Syncing:
    case State::Downloading:
    case State::Cancelling:
      back = tr(STR_CANCEL);
      break;
    case State::LoginConfirmed:
    case State::OpenBook:
      break;
  }
  const auto labels = mappedInput.mapLabels(back, confirm, previous, next);
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}

bool WeReadActivity::preventAutoSleep() { return isBusy(state_.load()); }
