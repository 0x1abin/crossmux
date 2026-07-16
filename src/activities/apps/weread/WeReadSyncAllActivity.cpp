#include "WeReadSyncAllActivity.h"

#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "../../../components/UITheme.h"
#include "../../../fontIds.h"
#include "../../ActivityManager.h"
#include "WeReadBookCacheFlow.h"
#include "WeReadCacheStore.h"
#include "WeReadKeyStore.h"
#include "WeReadModels.h"

WeReadSyncAllActivity::WeReadSyncAllActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("WeReadSyncAll", renderer, mappedInput) {}

void WeReadSyncAllActivity::onEnter() {
  Activity::onEnter();
  LOG_DBG("WRSYNC", "onEnter");

  if (WiFi.status() != WL_CONNECTED) {
    lastErr_ = WeReadClient::Err::NoWifi;
    phase_ = Phase::Error;
    requestUpdate();
    return;
  }
  if (!WeReadKeyStore::has()) {
    lastErr_ = WeReadClient::Err::NoApiKey;
    phase_ = Phase::Error;
    requestUpdate();
    return;
  }

  spawnShelfFetch();
  requestUpdate();
}

void WeReadSyncAllActivity::onExit() {
  ctx_.reset();
  taskHandle_ = nullptr;
  Activity::onExit();
}

// ---- Task plumbing ---------------------------------------------------------

void WeReadSyncAllActivity::fetchTrampoline(void* arg) {
  auto* wrapper = static_cast<std::shared_ptr<Context>*>(arg);
  std::shared_ptr<Context> ctx = *wrapper;
  delete wrapper;

  WeReadClient::Err err = WeReadClient::post(ctx->apiName.c_str(), *ctx->request, *ctx->response,
                                             /*httpTimeoutMs=*/10000, ctx->filter.get());
  ctx->filter.reset();
  ctx->err = static_cast<int>(err);
  ctx->state.store(err == WeReadClient::Err::Ok ? 1 : 2);
  // vTaskDelete(nullptr) doesn't return — drop the shared_ptr explicitly so
  // Context doesn't leak forever (otherwise back-to-back tasks exhaust the
  // heap within seconds).
  ctx.reset();
  vTaskDelete(nullptr);
}

void WeReadSyncAllActivity::spawnShelfFetch() {
  ctx_.reset();

  auto ctx = std::shared_ptr<Context>(new (std::nothrow) Context());
  if (!ctx) {
    LOG_ERR("WRSYNC", "Context OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }
  ctx->request = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  ctx->response = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  auto filterDoc = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  if (!ctx->request || !ctx->response || !filterDoc) {
    LOG_ERR("WRSYNC", "JsonDocument OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }

  // /shelf/sync takes no params; filter mirrors WeReadShelfActivity to keep
  // the response small on the fragmented heap.
  JsonDocument& filter = *filterDoc;
  filter["books"][0]["bookId"] = true;
  filter["books"][0]["title"] = true;
  filter["books"][0]["author"] = true;
  filter["books"][0]["category"] = true;
  filter["books"][0]["readUpdateTime"] = true;
  filter["books"][0]["finishReading"] = true;
  filter["books"][0]["isTop"] = true;
  filter["books"][0]["secret"] = true;
  filter["albums"][0]["albumInfo"]["albumId"] = true;
  filter["albums"][0]["albumInfo"]["name"] = true;
  filter["albums"][0]["albumInfo"]["authorName"] = true;
  filter["albums"][0]["albumInfo"]["finishStatus"] = true;
  filter["albums"][0]["albumInfoExtra"]["secret"] = true;
  filter["albums"][0]["albumInfoExtra"]["isTop"] = true;
  filter["mp"] = true;
  filter["errcode"] = true;
  filter["errmsg"] = true;
  filter["upgrade_info"] = true;
  ctx->apiName = "/shelf/sync";
  ctx->filter = std::move(filterDoc);
  ctx->state.store(0);

  auto* taskArg = new (std::nothrow) std::shared_ptr<Context>(ctx);
  if (!taskArg) {
    LOG_ERR("WRSYNC", "taskArg OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }
  BaseType_t rc = xTaskCreate(&fetchTrampoline, "WRSyncFetch", 4096, taskArg, 1, &taskHandle_);
  if (rc != pdPASS) {
    LOG_ERR("WRSYNC", "xTaskCreate failed");
    delete taskArg;
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }

  ctx_ = std::move(ctx);
  phase_ = Phase::FetchingShelf;
}

void WeReadSyncAllActivity::spawnBookStepFetch() {
  ctx_.reset();

  auto ctx = std::shared_ptr<Context>(new (std::nothrow) Context());
  if (!ctx) {
    LOG_ERR("WRSYNC", "Context OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }
  ctx->request = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  ctx->response = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  auto filterDoc = std::unique_ptr<JsonDocument>(new (std::nothrow) JsonDocument());
  if (!ctx->request || !ctx->response || !filterDoc) {
    LOG_ERR("WRSYNC", "JsonDocument OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }

  const std::string& bookId = booksToSync_[bookIdx_].first;
  WeReadBookCacheFlow::buildRequest(stepIdx_, bookId, *ctx->request, *filterDoc);
  ctx->apiName = WeReadBookCacheFlow::stepApiName(stepIdx_);
  ctx->filter = std::move(filterDoc);
  ctx->state.store(0);

  auto* taskArg = new (std::nothrow) std::shared_ptr<Context>(ctx);
  if (!taskArg) {
    LOG_ERR("WRSYNC", "taskArg OOM");
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }
  BaseType_t rc = xTaskCreate(&fetchTrampoline, "WRSyncFetch", 4096, taskArg, 1, &taskHandle_);
  if (rc != pdPASS) {
    LOG_ERR("WRSYNC", "xTaskCreate failed");
    delete taskArg;
    lastErr_ = WeReadClient::Err::Http;
    phase_ = Phase::Error;
    return;
  }

  ctx_ = std::move(ctx);
  phase_ = Phase::SyncingBook;
}

// ---- Shelf parsing ---------------------------------------------------------

void WeReadSyncAllActivity::parseShelfResponseAndSave() {
  if (!ctx_ || !ctx_->response) return;
  JsonDocument& resp = *ctx_->response;

  // Reuse WeReadShelfActivity's field mapping. shelf.bin gets the full
  // BookCard vector (including albums) so offline browsing matches online.
  // booksToSync_ collects only ebooks since the /book/* APIs reject
  // albumIds.
  std::vector<WeReadModels::BookCard> shelf;
  booksToSync_.clear();

  JsonArrayConst booksJson = resp["books"].as<JsonArrayConst>();
  if (!booksJson.isNull()) {
    shelf.reserve(booksJson.size());
    for (JsonVariantConst b : booksJson) {
      WeReadModels::BookCard card;
      card.bookId = b["bookId"] | "";
      card.title = b["title"] | "";
      card.author = b["author"] | "";
      card.category = b["category"] | "";
      card.readUpdateTime = b["readUpdateTime"] | 0u;
      card.finishReading = b["finishReading"] | 0;
      card.isTop = b["isTop"] | 0;
      card.secret = b["secret"] | 0;
      card.isAlbum = WeReadModels::kBookKindEbook;
      if (!card.bookId.empty()) {
        booksToSync_.emplace_back(card.bookId, card.title);
        shelf.push_back(std::move(card));
      }
    }
  }

  JsonArrayConst albumsJson = resp["albums"].as<JsonArrayConst>();
  if (!albumsJson.isNull()) {
    for (JsonVariantConst a : albumsJson) {
      JsonVariantConst info = a["albumInfo"];
      WeReadModels::BookCard card;
      card.bookId = info["albumId"] | "";
      card.title = info["name"] | "";
      card.author = info["authorName"] | "";
      card.category = info["finishStatus"] | "";
      card.secret = static_cast<uint8_t>(a["albumInfoExtra"]["secret"] | 0.0);
      card.isTop = static_cast<uint8_t>(a["albumInfoExtra"]["isTop"] | 0.0);
      card.isAlbum = WeReadModels::kBookKindAudio;
      if (!card.bookId.empty()) shelf.push_back(std::move(card));
    }
  }

  if (!resp["mp"].isNull()) {
    WeReadModels::BookCard card;
    card.bookId = "__weread_mp__";
    card.title = tr(STR_WEREAD_MP_COLLECTION);
    card.category = tr(STR_WEREAD_MP_COLLECTION_SUB);
    card.secret = 1;
    card.isAlbum = WeReadModels::kBookKindMp;
    shelf.push_back(std::move(card));
  }

  WeReadCacheStore::saveShelf(shelf);
  LOG_DBG("WRSYNC", "shelf saved: %u ebooks to sync", static_cast<unsigned>(booksToSync_.size()));
}

// ---- Main-loop state-machine drain ----------------------------------------

void WeReadSyncAllActivity::consumeResult() {
  if (!ctx_) return;
  const int s = ctx_->state.load();
  if (s == 0) return;  // task still running

  if (phase_ == Phase::FetchingShelf) {
    if (s == 2) {
      lastErr_ = static_cast<WeReadClient::Err>(ctx_->err);
      ctx_.reset();
      phase_ = Phase::Error;
      requestUpdate();
      return;
    }
    parseShelfResponseAndSave();
    ctx_.reset();
    bookIdx_ = 0;
    stepIdx_ = 0;
    syncedCount_ = 0;
    if (booksToSync_.empty()) {
      phase_ = Phase::Done;
      requestUpdate();
      return;
    }
    spawnBookStepFetch();
    requestUpdate();
    return;
  }

  // phase_ == SyncingBook — drain a per-step result.
  if (s == 1) {
    const bool savedOk = WeReadBookCacheFlow::parseAndSave(stepIdx_, booksToSync_[bookIdx_].first, *ctx_->response);
    ctx_.reset();
    if (!savedOk) {
      LOG_ERR("WRSYNC", "SD write failed at book %d step %d", bookIdx_, stepIdx_);
      lastErr_ = WeReadClient::Err::Http;
      phase_ = Phase::Error;
      requestUpdate();
      return;
    }
  } else {
    const WeReadClient::Err err = static_cast<WeReadClient::Err>(ctx_->err);
    ctx_.reset();
    if (WeReadBookCacheFlow::isFatalErr(err)) {
      lastErr_ = err;
      phase_ = Phase::Error;
      requestUpdate();
      return;
    }
    // Non-fatal: write an empty .bin and advance.
    LOG_DBG("WRSYNC", "book %d step %d %s (non-fatal); writing empty", bookIdx_, stepIdx_,
            WeReadClient::errorName(err));
    WeReadBookCacheFlow::saveEmpty(stepIdx_, booksToSync_[bookIdx_].first);
  }

  ++stepIdx_;
  if (stepIdx_ >= WeReadBookCacheFlow::kTotalSteps) {
    ++syncedCount_;
    ++bookIdx_;
    stepIdx_ = 0;
    LOG_DBG("WRSYNC", "book %d/%u done", bookIdx_, static_cast<unsigned>(booksToSync_.size()));
    if (bookIdx_ >= static_cast<int>(booksToSync_.size())) {
      LOG_DBG("WRSYNC", "all books done, synced=%d", syncedCount_);
      phase_ = Phase::Done;
      requestUpdate();
      return;
    }
  }
  spawnBookStepFetch();
  requestUpdate();
}

void WeReadSyncAllActivity::loop() {
  consumeResult();

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if ((phase_ == Phase::Done || phase_ == Phase::Error) &&
      mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (phase_ == Phase::Error) {
      // Retry from the top — shelf may still be unfetched.
      if (booksToSync_.empty()) {
        spawnShelfFetch();
      } else {
        spawnBookStepFetch();
      }
      requestUpdate();
    } else {
      finish();
    }
  }
}

void WeReadSyncAllActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();

  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, sw, metrics.headerHeight}, tr(STR_WEREAD_SYNC_ALL));

  const int contentY = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentH = sh - contentY - metrics.buttonHintsHeight - metrics.verticalSpacing;

  static char line1[160];
  static char line2[96];

  if (phase_ == Phase::Error) {
    const char* msg = (lastErr_ == WeReadClient::Err::NoWifi)     ? tr(STR_WEREAD_NO_WIFI)
                      : (lastErr_ == WeReadClient::Err::NoApiKey) ? tr(STR_WEREAD_NO_API_KEY)
                      : (lastErr_ == WeReadClient::Err::Upgrade)  ? tr(STR_WEREAD_UPGRADE_REQUIRED)
                      : (lastErr_ == WeReadClient::Err::Server)   ? tr(STR_WEREAD_SERVER_ERROR)
                                                                  : tr(STR_WEREAD_HTTP_ERROR);
    GUI.drawPopup(renderer, msg);
  } else if (phase_ == Phase::Done) {
    std::snprintf(line1, sizeof(line1), tr(STR_WEREAD_SYNC_ALL_DONE_FMT), syncedCount_);
    GUI.drawPopup(renderer, line1);
  } else if (phase_ == Phase::FetchingShelf) {
    GUI.drawPopup(renderer, tr(STR_WEREAD_LOADING));
  } else {
    // SyncingBook — two-line status + book-level progress bar.
    const int total = static_cast<int>(booksToSync_.size());
    const int curBook = bookIdx_ + 1;
    const std::string& title = (bookIdx_ < total) ? booksToSync_[bookIdx_].second : std::string{};
    std::snprintf(line1, sizeof(line1), tr(STR_WEREAD_SYNC_ALL_PROGRESS_FMT), curBook, total, title.c_str());
    std::snprintf(line2, sizeof(line2), tr(STR_WEREAD_CACHE_PROGRESS_FMT), stepIdx_ + 1,
                  WeReadBookCacheFlow::kTotalSteps, WeReadBookCacheFlow::stepName(stepIdx_));
    const int centerY = contentY + contentH / 2;
    renderer.drawCenteredText(UI_12_FONT_ID, centerY - metrics.verticalSpacing * 2, line1);
    renderer.drawCenteredText(UI_12_FONT_ID, centerY, line2);
    const int barY = centerY + metrics.verticalSpacing * 2;
    GUI.drawProgressBar(
        renderer,
        Rect{metrics.contentSidePadding, barY, sw - metrics.contentSidePadding * 2, metrics.progressBarHeight},
        static_cast<size_t>(bookIdx_), static_cast<size_t>(total > 0 ? total : 1));
  }

  const char* btn2 = (phase_ == Phase::Error) ? tr(STR_RETRY) : (phase_ == Phase::Done) ? tr(STR_BACK) : "";
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), btn2, "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
