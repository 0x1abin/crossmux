#pragma once

#include <cstdint>

#include "../WeReadBackend.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

struct Rect;

class WeReadBrowseActivity final : public Activity {
 public:
  WeReadBrowseActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, WeReadClient::Operation& operation,
                       const WeReadStore::ShelfRecord& book)
      : Activity("WeReadBrowse", renderer, mappedInput), operation_(operation), book_(WeReadStore::bookRecord(book)) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class State : uint8_t { Menu, Loading, List, Detail, Error };
  enum class MenuAction : uint8_t { PopularHighlights, MyHighlights, PopularReviews, Refresh };
  static constexpr int kMenuCount = 4;
  static constexpr int kMaxTextPages = 128;

  WeReadClient::Operation& operation_;
  WeReadStore::BookRecord book_;
  ButtonNavigator navigator_;
  HalFile index_;
  HalFile text_;
  WeReadBrowse::CacheManifest cache_;
  WeReadBrowse::PageHeader pageHeader_;
  WeReadBrowse::Record selectedRecord_;
  uint32_t textPageOffsets_[kMaxTextPages + 1] = {};
  uint32_t currentPage_ = 0;
  State state_ = State::Menu;
  WeReadBrowse::Kind kind_ = WeReadBrowse::Kind::PopularHighlights;
  WeReadClient::Error error_ = WeReadClient::Error::Ok;
  int menuSelected_ = 0;
  int listSelected_ = 0;
  int textPage_ = 0;
  int textPageCount_ = 1;
  int readerFontId_ = 0;
  bool qrReady_ = false;
  bool hasCache_ = false;
  bool textPagesTruncated_ = false;
  bool readerFontReady_ = false;
  bool wifiReleasePending_ = false;

  Rect contentBounds() const;
  int pageItemCount() const;
  const char* kindTitle() const;
  bool reloadCache();
  void closePage();
  bool openPage(uint32_t page);
  bool readRecord(uint32_t index, WeReadBrowse::Record& record);
  std::string rowTitle(int index);
  std::string rowSubtitle(int index);
  void connectThenCache();
  void startCache();
  void stepLoad();
  void activateMenu();
  void activateList();
  void releaseReaderFont();
  void openDetail();
  void buildTextPages();
  void drawDetail(const Rect& content);
  void handleMenuInput();
  void handleListInput();
  void handleDetailInput();
  void handleErrorInput();
  const char* errorMessage() const;
};
