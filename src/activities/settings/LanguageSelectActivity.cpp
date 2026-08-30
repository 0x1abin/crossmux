#include "LanguageSelectActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <iterator>

#include "CrossPointSettings.h"
#include "I18nKeys.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

namespace fui = freeink::ui;

namespace {
constexpr CrossPointSettings::ContentProfile initialProfileFor(const Language language) {
  return language == Language::ZH_CN ? CrossPointSettings::ContentProfile::China
                                     : CrossPointSettings::ContentProfile::Global;
}

static_assert(initialProfileFor(Language::ZH_CN) == CrossPointSettings::ContentProfile::China);
static_assert(initialProfileFor(Language::EN) == CrossPointSettings::ContentProfile::Global);
}  // namespace

LanguageSelectActivity::LanguageSelectActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Mode mode)
    : UiListActivity("LanguageSelect", renderer, mappedInput), mode_(mode) {}

void LanguageSelectActivity::onEnter() {
  UiListActivity::onEnter();

  // Initial onboarding offers Simplified Chinese first without changing the
  // active language until the user confirms. Other entry modes keep the
  // current language selected.
  const auto selectedLang =
      mode_ == Mode::Initial ? static_cast<uint8_t>(Language::ZH_CN) : static_cast<uint8_t>(I18N.getLanguage());
  const auto* begin = std::begin(SORTED_LANGUAGE_INDICES);
  const auto* end = std::end(SORTED_LANGUAGE_INDICES);
  const auto* it = std::find(begin, end, selectedLang);
  nav.selected = (it != end) ? static_cast<int>(std::distance(begin, it)) : 0;

  // Built once here rather than every buildScreen() call: labels are static,
  // and the "Selected" marker can't go stale mid-visit since activateIndex()
  // finishes the activity immediately on selection.
  for (int i = 0; i < totalItems; ++i) {
    fui::ListItem item;
    item.label = I18N.getLanguageName(static_cast<Language>(SORTED_LANGUAGE_INDICES[i]));
    if (SORTED_LANGUAGE_INDICES[i] == selectedLang) {
      item.value = tr(STR_SELECTED);
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems[i] = item;
  }
}

const char* LanguageSelectActivity::headerTitle() const { return tr(STR_LANGUAGE); }

void LanguageSelectActivity::activateIndex(const int index) {
  // The activated row leaves this screen; a lingering flash would gray an
  // unrelated element on the next render.
  app.clearTapFlash();
  nav.selected = index;
  const uint8_t langIndex = SORTED_LANGUAGE_INDICES[index];

  const uint8_t previousLanguage = SETTINGS.language;
  const auto previousContentProfile = SETTINGS.contentProfile;
  const uint8_t previousClockUtcOffsetQ = SETTINGS.clockUtcOffsetQ;
  const uint8_t previousFontFamily = SETTINGS.fontFamily;
  const uint8_t previousFontPointSize = SETTINGS.fontPointSize;
  const uint32_t previousHiddenAppsMask = SETTINGS.hiddenAppsMask;
  const uint8_t previousOnboardingVersion = SETTINGS.onboardingVersion;
  SETTINGS.language = langIndex;
  const Language language = static_cast<Language>(langIndex);
  const bool simplifiedChinese = language == Language::ZH_CN;
  switch (mode_) {
    case Mode::Settings:
      break;
    case Mode::Initial:
      SETTINGS.contentProfile = initialProfileFor(language);
      SETTINGS.clockUtcOffsetQ = simplifiedChinese ? 80 : 48;
      SETTINGS.fontFamily = CrossPointSettings::NOTOSANS;
      SETTINGS.fontPointSize = 12;
      if (simplifiedChinese) {
        SETTINGS.hiddenAppsMask &= ~CrossPointSettings::CHINA_ONLY_APPS_MASK;
      } else {
        SETTINGS.hiddenAppsMask |= CrossPointSettings::CHINA_ONLY_APPS_MASK;
      }
      SETTINGS.onboardingVersion = CrossPointSettings::CURRENT_ONBOARDING_VERSION;
      break;
    case Mode::Upgrade:
      SETTINGS.contentProfile = initialProfileFor(language);
      SETTINGS.onboardingVersion = CrossPointSettings::CURRENT_ONBOARDING_VERSION;
      break;
  }
  if (!SETTINGS.saveToFile()) {
    SETTINGS.language = previousLanguage;
    SETTINGS.contentProfile = previousContentProfile;
    SETTINGS.clockUtcOffsetQ = previousClockUtcOffsetQ;
    SETTINGS.fontFamily = previousFontFamily;
    SETTINGS.fontPointSize = previousFontPointSize;
    SETTINGS.hiddenAppsMask = previousHiddenAppsMask;
    SETTINGS.onboardingVersion = previousOnboardingVersion;
    LOG_ERR("LANG", "Failed to save language selection");
    for (int i = 0; i < totalItems; ++i) {
      rowItems[i].value = SORTED_LANGUAGE_INDICES[i] == langIndex ? tr(STR_SELECTED) : nullptr;
    }
    requestUpdate();
    return;
  }

  {
    RenderLock lock(*this);
    I18N.setLanguage(language);
  }

  if (isOnboarding()) {
#ifndef SIMULATOR
    halClock.setUseChinaServers(SETTINGS.contentProfile == CrossPointSettings::ContentProfile::China);
#endif
    onGoHome();
  } else {
    finish();
  }
}

void LanguageSelectActivity::onBackButton() {
  if (!isOnboarding()) finish();
}

void LanguageSelectActivity::drawFooter() {
  const auto labels =
      mappedInput.mapLabels(isOnboarding() ? "" : tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
}

void LanguageSelectActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  // Content: the safe area minus the header band GUI.drawHeader paints.
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
                                      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
                                      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)),
                                      static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  // rowItems was built once in onEnter() and is reused here on every repaint.
  fui::ListProps props;
  props.items = rowItems;
  props.count = static_cast<uint16_t>(totalItems);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  // Label at the value's font size: both sides of the row read as one unit.
  // maxLines=2 also marks the style caller-owned (see textStyleUnset).
  props.labelText = screen.theme().smallText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}
