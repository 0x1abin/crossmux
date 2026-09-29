#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "EndOfBookOptions.h"
#include "activities/Activity.h"

class ReaderActivity : public Activity {
 protected:
  std::string bookPath;
  int pagesUntilFullRefresh = 0;
  bool forcedRefreshPending = false;

  std::unique_ptr<EndOfBookOptions> endOfBookOptions;
  std::atomic<bool> endOfBookOptionsReady{false};

  explicit ReaderActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                          std::string bookPath, bool allowFastInitialRefresh);

  virtual bool loadBook() = 0;
  virtual std::string getBookTitle() const = 0;
  virtual std::string getBookAuthor() const { return ""; }
  virtual std::string getBookThumbBmpPath() const { return ""; }

  virtual bool handleFormatInput() { return false; }
  virtual bool pageTurn(bool isForward) = 0;
  virtual bool skipPages(int amount) { return pageTurn(amount > 0); }
  virtual bool isAtEndOfBook() const = 0;
  virtual void onReturnFromEndOfBook() {}

  // Tap-zone action dispatch. The base implementation handles the actions that
  // every reader shares (orientation, frontlight, home); subclasses extend it
  // with reader-specific actions (bookmark / dictionary / chapter / percent /
  // KOReader / auto turn). Returning false leaves the action unhandled, which
  // the loop treats as "not supported on this reader" and swallows.
  virtual bool handleZoneShortAction(uint8_t action);
  virtual bool handleZoneLongAction(uint8_t action);

  // Rotate the reading orientation by +1 (clockwise) / -1 (counter-clockwise)
  // and toggle the frontlight. Shared by all readers; orientation reflows are
  // handled by each reader's own rendering pass on the next update.
  void rotateOrientation(int delta);
  void toggleFrontlight();

  virtual void renderBook() = 0;
  virtual void applyInitialOrientation();
  virtual void onEndOfBookRendered() {}

  bool handleBackNavigation();
  /** True while the end-of-book suggestion menu is on screen and owning input. */
  bool endOfBookMenuActive() const;
  bool handleEndOfBookMenu(bool suppressConfirmRelease = false);
  bool handleEndOfBookPageTurn(bool prevTriggered, bool nextTriggered);
  void clearEndOfBookOptionsIfNeeded();
  void disableFastInitialRefresh();

 public:
  ~ReaderActivity() override = default;

  static std::unique_ptr<ReaderActivity> create(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                std::string path, bool allowFastInitialRefresh);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&& lock) override;

  bool isReaderActivity() const final { return true; }
  bool handleForcedRefresh() final;
};
