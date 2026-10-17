#include "UiListActivity.h"

#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <optional>

#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "components/UIThemeTokens.h"
#if FREEINK_CAP_TOUCH
#include "components/UiAppHelpers.h"
#endif
#include "fontIds.h"

namespace fui = freeink::ui;

UiListActivity::UiListActivity(const char* name, GfxRenderer& renderer, MappedInputManager& mappedInput,
                               const bool wantsTouchLongPress, const bool upstreamStyle)
    : Activity(name, renderer, mappedInput),
      UiAppHost(renderer, upstreamStyle),
      wantsTouchLongPress(wantsTouchLongPress) {}

void UiListActivity::onEnter() {
  Activity::onEnter();
  contentSelection.reset(mappedInput, defersContentSelection());
  activeNav().reset();
  resetUi();
  app.on(ACTION_ROW, &UiListActivity::rowActionTrampoline, this);
#if FREEINK_CAP_TOUCH
  revealedIndex.store(-1);
  app.on(ACTION_SWIPE_DELETE, &UiListActivity::swipeDeleteTrampoline, this);
#endif
  app.setScreen(&UiListActivity::screenTrampoline, this);
  requestUpdate();
}

void UiListActivity::screenTrampoline(UiScreen& screen, void* user) {
  static_cast<UiListActivity*>(user)->buildScreen(screen);
}

void UiListActivity::rowActionTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.value < 0 || event.value >= self->listCount()) return;
#if FREEINK_CAP_TOUCH
  if (self->revealedIndex.load() >= 0) {
    self->closeSwipeDelete();
    self->app.clearTapFlash();
    return;
  }
#endif
  self->establishContentSelection(event.value);
  self->onRowAction(event);
}

#if FREEINK_CAP_TOUCH
void UiListActivity::swipeDeleteTrampoline(const fui::ActionEvent& event, void* user) {
  auto* self = static_cast<UiListActivity*>(user);
  if (event.value != self->revealedIndex.load() || !self->canSwipeDelete(event.value)) return;
  self->closeSwipeDelete();
  self->app.clearTapFlash();
  self->establishContentSelection(event.value);
  self->swipeDelete(event.value);
}

void UiListActivity::closeSwipeDelete() {
  if (revealedIndex.exchange(-1) >= 0) requestUpdate();
}

void UiListActivity::configureSwipeDelete(UiScreen& screen, fui::ListProps& props) {
  const int row = revealedIndex.load();
  if (row < 0 || row >= listCount() || !canSwipeDelete(row)) return;
  swipeReveal.index = static_cast<int16_t>(row);
  swipeReveal.action = ACTION_SWIPE_DELETE;
  swipeReveal.icon = fui::bitmapFromIcon(icon_trash_2_24);
  swipeReveal.width = static_cast<int16_t>(screen.theme().rowHeight + screen.theme().spaceSm * 2);
  props.reveal = &swipeReveal;
}

bool UiListActivity::handleSwipeDeleteInput() {
  if (!mappedInput.hasTouch()) return false;
  const int openRow = revealedIndex.load();
  if (openRow >= 0) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      closeSwipeDelete();
      return true;
    }
    int x = 0;
    int y = 0;
    if (mappedInput.wasScreenTapped(x, y)) {
      fui::Interaction hit;
      bool onDelete;
      {
        RenderLock lock(*this);
        onDelete = routingReady() && app.hitPublished(x, y, ACTION_SWIPE_DELETE, hit) && hit.value == openRow;
        if (!onDelete && routingReady())
          app.route(fui::InputSnapshot{.touchReleased = true, .touchX = -1, .touchY = -1});
      }
      if (!onDelete) {
        closeSwipeDelete();
        return true;
      }
    }
  }

  int x = 0;
  int y = 0;
  const auto swipe = mappedInput.wasSwipe(&x, &y);
  if (swipe == MappedInputManager::SwipeDir::Right && openRow >= 0) {
    UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
    closeSwipeDelete();
    return true;
  }
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    closeSwipeDelete();
    return false;
  }
  if (swipe != MappedInputManager::SwipeDir::Left) return false;

  fui::Interaction hit;
  {
    RenderLock lock(*this);
    if (!routingReady() || !app.hitPublished(x, y, ACTION_ROW, hit) || !canSwipeDelete(hit.value)) return false;
  }
  UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
  revealedIndex.store(hit.value);
  requestUpdate();
  return true;
}
#endif

void UiListActivity::onRowAction(const fui::ActionEvent& event) {
  activeNav().selected = event.value;
  if (event.longPress) {
    onRowLongPress(event.value);
    return;
  }
  activateIndex(event.value);
}

bool UiListActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    onBackButton();
    return true;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    const int selected = activeNav().selected;
    if (selected >= 0 && selected < listCount()) activateIndex(selected);
    return true;
  }
  return false;
}

bool UiListActivity::routeListTouch() {
  int touchX = 0;
  int touchY = 0;
  if (mappedInput.wasScreenTouchPressed(touchX, touchY))
    LOG_DBG("LIST", "%s touch down x=%d y=%d", name.c_str(), touchX, touchY);
  // Touch goes through the FreeInkApp: render() registered the row hit rects;
  // route the snapshot and let the action trampoline dispatch.
  if (mappedInput.wasScreenTouchReleased()) LOG_DBG("LIST", "%s touch release", name.c_str());
  const auto route = UiAppHost::routeTouch(mappedInput, wantsTouchLongPress);
  // No pressed-state repaint: the render it triggers would drop a slow tap's
  // release inside the uiReady window (tap-to-activate needed two taps), and
  // it costs a second e-ink refresh per tap.
  if (route.routed && app.invalidated()) requestUpdate();
  return static_cast<bool>(route);  // dispatched to the action handler
}

void UiListActivity::moveSelectionTo(const int index) {
  if (!hasContentSelection()) contentSelection.guardHeld(mappedInput);
#if FREEINK_CAP_TOUCH
  closeSwipeDelete();
#endif
  activeNav().requestSelection(index);
  establishContentSelection(index);
  requestUpdate();
}

bool UiListActivity::hasContentSelection() const {
  return !defersContentSelection() || (contentSelection.hasSelection() && listCount() > 0);
}

void UiListActivity::clearContentSelection() { contentSelection.clear(); }

void UiListActivity::establishContentSelection(const int index) {
  if (!defersContentSelection() || index < 0 || index >= listCount()) return;
  activeNav().selected = index;
  activityManager.focusMainTabContent();
  contentSelection.establish();
}

bool UiListActivity::showMainTabContentSelection() const {
  return defersContentSelection() ? contentSelectionVisibleForFrame : Activity::showMainTabContentSelection();
}

bool UiListActivity::consumeSelectionEntryInput() {
  const auto entry = contentSelection.consumeEntry(mappedInput, defersContentSelection(), hasContentSelection());
  using Entry = UiContentSelection::Entry;
  switch (entry) {
    case Entry::None:
      return false;
    case Entry::Consumed:
      return true;
    case Entry::Confirm:
      if (listCount() > 0) moveSelectionTo(initialConfirmIndex());
      return true;
    case Entry::First:
      if (listCount() > 0) moveSelectionTo(0);
      return true;
    case Entry::Last:
      if (listCount() > 0) moveSelectionTo(listCount() - 1);
      return true;
  }
  return false;
}

void UiListActivity::loop() {
  if (handleCustomInput()) return;
  const bool selectionEntryConsumed = consumeSelectionEntryInput();
#if FREEINK_CAP_TOUCH
  if (handleSwipeDeleteInput()) return;
#endif
  if (!selectionEntryConsumed && handleButtons()) return;
  if (routeListTouch()) return;

  // Swipes scroll the viewport; the selection stays put (it may scroll
  // off-screen) and button navigation pulls the view back to it.
  int startX = 0;
  int startY = 0;
  const auto swipe = mappedInput.wasSwipe(&startX, &startY);
  if (handleListSwipe(swipe, startX, startY)) return;

  if (!selectionEntryConsumed) navigateButtons();
}

bool UiListActivity::handleListSwipe(const MappedInputManager::SwipeDir swipe, const int, const int) {
  if (swipe == MappedInputManager::SwipeDir::Up || swipe == MappedInputManager::SwipeDir::Down) {
    if (defersContentSelection()) activityManager.focusMainTabContent();
    auto& n = activeNav();
    const int delta = swipe == MappedInputManager::SwipeDir::Up ? n.inputPageRows() : -n.inputPageRows();
    LOG_DBG("LIST", "%s swipe delta=%d count=%d", name.c_str(), delta, listCount());
    n.requestScroll(delta);
    requestUpdate();
    return true;
  }
  return false;
}

void UiListActivity::navigateButtons() {
  buttonNavigator.onNextPress(
      [this] { moveSelectionTo(ButtonNavigator::nextIndex(activeNav().selected, listCount())); });
  buttonNavigator.onPreviousPress(
      [this] { moveSelectionTo(ButtonNavigator::previousIndex(activeNav().selected, listCount())); });
  // Page by the rows the last build actually drew (pageRows), not the
  // fixed-height visibleRows estimate: with wrapped labels the estimate
  // overshoots and rows between pages would never be shown. The measurement
  // can be one build old while a refresh is in flight; the next layout's
  // feedback corrects the viewport.
  buttonNavigator.onNextContinuous([this] {
    const auto& n = activeNav();
    moveSelectionTo(ButtonNavigator::nextPageIndex(n.selected, listCount(), n.inputPageRows()));
  });
  buttonNavigator.onPreviousContinuous([this] {
    const auto& n = activeNav();
    moveSelectionTo(ButtonNavigator::previousPageIndex(n.selected, listCount(), n.inputPageRows()));
  });
}

void UiListActivity::syncListViewport(UiScreen& screen, fui::ListProps& props, const int selectionOffset) {
  if (usesUpstreamStyle() || SETTINGS.uiTheme != CrossPointSettings::INX) {
    props.toggleCheckbox = true;
    props.toggleWidth = 28;
    props.toggleHeight = 28;
  }
  props.partialTrailingRow = true;
  auto& n = activeNav();
  const int prevTop = n.top;
  const bool trusted = n.trusts(listCount());
  const int drawn = n.drawnRows;

  screen.syncListViewport(n, props, listCount(), selectionOffset);
  if (defersContentSelection() && !showMainTabContentSelection()) props.selectedIndex = -1;
  // Tab focus intentionally hides the row selection; it cannot satisfy a follow repaint.
  if (!showMainTabContentSelection()) n.followPending = false;

  // When the selection is already visible in the current viewport (based on
  // the measured drawnRows rather than the unweighted visibleRows estimate),
  // keep selection-follow anchored instead of jumping to top. Explicit swipe
  // scrolling clears followPending and must retain its new viewport.
  if (n.followPending && trusted && drawn > 0) {
    const int sel = props.selectedIndex;
    if (sel >= prevTop && sel < prevTop + drawn) {
      n.top = prevTop;
      props.topIndex = static_cast<uint16_t>(prevTop);
    }
  }
}

void UiListActivity::syncPagedViewport(const int pageItems) {
  auto& n = activeNav();
  fui::ListProps props;
  const int rows = std::max(1, pageItems);
  n.syncToProps(fui::Rect{0, 0, 1, static_cast<int16_t>(rows)}, 1, 0, listCount(), props);
  n.drawnCount = listCount();
  n.onListRendered(props.topIndex, std::min(rows, std::max(0, listCount() - n.top)),
                   props.selectedIndex >= n.top && props.selectedIndex < n.top + rows);
}

void UiListActivity::drawChrome() {
  const char* title = headerTitle();
  if (!title) return;
  const auto& metrics = uiThemeMetrics(usesUpstreamStyle());
  if (usesUpstreamStyle()) {
    GUI.drawHeaderWithStyle(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                            title, nullptr, true, true);
  } else {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight}, title);
  }
}

void UiListActivity::drawFooter() {
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHintsWithStyle(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4, usesUpstreamStyle());
}

void UiListActivity::render(RenderLock&&) {
  const auto started = millis();
  contentSelectionVisibleForFrame = Activity::showMainTabContentSelection() && UiListActivity::hasContentSelection();
  std::optional<FontCacheManager::PrewarmScope> prewarm;
  if (auto* fonts = renderer.getFontCacheManager(); fonts && !fonts->isScanning()) {
    prewarm.emplace(fonts->createUiCjkPrewarmScope());
    drawChrome();
    scanUi();
    for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
      drawChrome();
      scanUi();
    }
    drawFooter();
    prewarm->endScanAndPrewarm();
  }
  const auto scanned = millis();
  // Bitmap targets can write pixels during scanning; discard that temporary paint.
  renderer.clearScreen();
  drawChrome();
  renderUi();
  // Wrapped labels grow rows, so fewer rows can fit than the fixed-height
  // estimate ListNav plans with. list() reports the real layout back
  // (ListNav::onListRendered); when the selection landed past the drawn rows
  // the nav advanced the viewport and asked for another build. Bounded: top
  // strictly advances toward the selection each pass.
  for (int pass = 0; activeNav().consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  drawFooter();
  const auto painted = millis();
  renderer.displayBuffer();
  LOG_DBG("LIST", "%s scan=%lu paint=%lu display=%lu ms", name.c_str(), scanned - started, painted - scanned,
          millis() - painted);
}
