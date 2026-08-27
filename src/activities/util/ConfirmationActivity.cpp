#include "ConfirmationActivity.h"

#include <I18n.h>

#include <algorithm>

#include "HalDisplay.h"
#include "components/UITheme.h"

ConfirmationActivity::ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const std::string& heading, const std::string& body,
                                           const BodyPlacement bodyPlacement)
    : Activity("Confirmation", renderer, mappedInput), heading(heading), body(body), bodyPlacement(bodyPlacement) {}

void ConfirmationActivity::onEnter() {
  Activity::onEnter();

  const int maxWidth = renderer.getScreenWidth() - (MARGIN * 2);

  if (!heading.empty()) {
    safeHeading = renderer.truncatedText(HEADING_FONT_ID, heading.c_str(), maxWidth, EpdFontFamily::BOLD);
  }
  if (!body.empty()) {
    if (bodyPlacement == BodyPlacement::Page) {
      auto lines = renderer.wrappedText(BODY_FONT_ID, body.c_str(), maxWidth, MAX_BODY_LINES, EpdFontFamily::REGULAR);
      if (!lines.empty()) safeBody = std::move(lines[0]);
      if (lines.size() > 1) safeBodySecondLine = std::move(lines[1]);
    } else {
      safeBody = renderer.truncatedText(BODY_FONT_ID, body.c_str(), maxWidth, EpdFontFamily::REGULAR);
    }
  }

  const char* options[] = {I18N.get(StrId::STR_CANCEL), I18N.get(StrId::STR_CONFIRM)};
  const char* popupTitle = nullptr;
  switch (bodyPlacement) {
    case BodyPlacement::Page:
      popupTitle = safeHeading.c_str();
      break;
    case BodyPlacement::PopupTitle:
      popupTitle = safeBody.c_str();
      break;
  }
  confirmPopup.show(popupTitle, options, 2, 0, [this](int idx) {
    ActivityResult res;
    res.isCancelled = (idx != 1);
    setResult(std::move(res));
    finish();
  });

  requestUpdate(true);
}

void ConfirmationActivity::render(RenderLock&& lock) {
  renderer.clearScreen();

  const int headingHeight = safeHeading.empty() ? 0 : renderer.getLineHeight(HEADING_FONT_ID);
  const int bodyLineHeight = renderer.getLineHeight(BODY_FONT_ID);
  const int bodyLines = safeBody.empty() ? 0 : (safeBodySecondLine.empty() ? 1 : 2);
  const int textBlockHeight =
      headingHeight + (headingHeight > 0 && bodyLines > 0 ? SPACING : 0) + bodyLines * bodyLineHeight;
  int currentY = std::max(MARGIN, renderer.getScreenHeight() / 6 - textBlockHeight / 2);
  if (!safeHeading.empty()) {
    renderer.drawCenteredText(HEADING_FONT_ID, currentY, safeHeading.c_str(), true, EpdFontFamily::BOLD);
    currentY += headingHeight + SPACING;
  }

  switch (bodyPlacement) {
    case BodyPlacement::Page:
      if (!safeBody.empty()) {
        renderer.drawCenteredText(BODY_FONT_ID, currentY, safeBody.c_str(), true, EpdFontFamily::REGULAR);
      }
      if (!safeBodySecondLine.empty()) {
        renderer.drawCenteredText(BODY_FONT_ID, currentY + bodyLineHeight, safeBodySecondLine.c_str(), true,
                                  EpdFontFamily::REGULAR);
      }
      break;
    case BodyPlacement::PopupTitle:
      break;
  }

  if (confirmPopup.processRender(renderer, mappedInput)) return;

  renderer.displayBuffer(HalDisplay::RefreshMode::FAST_REFRESH);
}

void ConfirmationActivity::loop() {
  if (confirmPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Popup dismissed without a selection (Back button or tap outside): cancel.
  ActivityResult res;
  res.isCancelled = true;
  setResult(std::move(res));
  finish();
}
