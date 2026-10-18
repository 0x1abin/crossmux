#include "UiAppHost.h"

#include "UiAppHelpers.h"

namespace fui = freeink::ui;

UiAppHost::UiAppHost(const GfxRenderer& renderer, const bool upstreamStyle)
    : uiTarget(makeUiTarget(renderer, upstreamStyle)),
      app(uiTarget, uiTarget.deviceContext()),
      upstreamStyle(upstreamStyle) {}

void UiAppHost::resetUi() {
  uiReady = false;
  applySharedUiTheme(app, uiTarget, upstreamStyle);
}

void UiAppHost::prepareUi() {
  applyUiTextAlignment(uiTarget, upstreamStyle);
  const auto fonts = uiScaleSpec(upstreamStyle);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_SMALL,
                   !upstreamStyle && UITheme::getInstance().hasMainTabs() ? SMALL_FONT_ID : fonts.smallFontId);
  uiTarget.setFont(fui::GfxRendererTarget::FONT_BODY, fonts.bodyFontId);
  app.setDevice(uiTarget.deviceContext());
  refreshSharedUiThemeTokens(uiTarget, upstreamStyle);
}

void UiAppHost::renderUi() {
  prepareUi();
  app.render();
  uiReady = true;
}

void UiAppHost::scanUi() {
  prepareUi();
  app.paintForScan();
}

UiAppHost::TouchRoute UiAppHost::routeTouch(const MappedInputManager& input, const bool withLongPress,
                                            const bool routeHeld) {
  TouchRoute result;  // named apart from route() — cppcheck flags the shadow
  if (!uiReady) return result;
  result.snap = touchSnapshotFrom(input, withLongPress);
  if (!result.snap.touchPressed && !result.snap.touchReleased && !(routeHeld && result.snap.touchHeld)) {
    return result;
  }
  result.routed = true;
  result.event = route(result.snap);
  return result;
}

fui::ActionEvent UiAppHost::route(const fui::InputSnapshot& snap) {
  if (!uiReady) return {};
  const auto event = app.route(snap);
  // Releases activate controls; held/pressed frames only highlight or drag.
  // Empty/disabled hit regions produce no event and therefore no feedback.
  if (event && (snap.touchReleased || snap.swipeLeft || snap.swipeRight)) {
    haptic_feedback::touchAction(event.longPress);
  }
  return event;
}
