#include "TextSettingsActivity.h"

#include <GfxRenderer.h>
#include <HalDisplay.h>
#include <I18n.h>
#include <Logging.h>
#include <SdCardFontCache.h>
#ifdef ENABLE_CHINESE_VERSION
#include <Memory.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "ReaderFontSizes.h"
#include "SdCardFontSystem.h"
#include "TextSettingsPreview.h"
#ifdef ENABLE_CHINESE_VERSION
#include "activities/settings/FontDownloadActivity.h"
#endif
#include "activities/util/IntervalSelectionActivity.h"
#include "components/FontPreloadView.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace fui = freeink::ui;

namespace {
// Tab labels for Font | Size | Layout | Style.
constexpr StrId TAB_NAME_IDS[] = {StrId::STR_FONT, StrId::STR_SIZE, StrId::STR_LAYOUT, StrId::STR_STYLE};

constexpr StrId LAYOUT_ROW_NAME_IDS[] = {StrId::STR_LINE_SPACING, StrId::STR_EXTRA_SPACING, StrId::STR_ALIGNMENT,
                                         StrId::STR_SCREEN_MARGIN};
constexpr StrId STYLE_ROW_NAME_IDS[] = {StrId::STR_FOCUS_READING,
                                        StrId::STR_READING_GUIDE_LINE,
                                        StrId::STR_READING_GUIDE_LINE_STYLE,
                                        StrId::STR_READING_GUIDE_LINE_OFFSET,
                                        StrId::STR_HYPHENATION,
                                        StrId::STR_EMBEDDED_STYLE,
                                        StrId::STR_FAKE_BOLD,
                                        StrId::STR_TEXT_AA};

constexpr StrId LINE_SPACING_IDS[] = {StrId::STR_TIGHT, StrId::STR_NORMAL, StrId::STR_WIDE, StrId::STR_EXTRA_WIDE};
constexpr StrId SYNTHETIC_BOLD_IDS[] = {StrId::STR_STATE_OFF, StrId::STR_FAKE_BOLD_LIGHT, StrId::STR_FAKE_BOLD_STANDARD,
                                        StrId::STR_FAKE_BOLD_HEAVY};
static_assert(std::size(SYNTHETIC_BOLD_IDS) == CrossPointSettings::SYNTHETIC_BOLD_COUNT);
constexpr StrId ALIGNMENT_IDS[] = {StrId::STR_JUSTIFY, StrId::STR_ALIGN_LEFT, StrId::STR_CENTER, StrId::STR_ALIGN_RIGHT,
                                   StrId::STR_BOOK_S_STYLE};
constexpr StrId GUIDE_LINE_STYLE_IDS[] = {StrId::STR_SOLID_LINE, StrId::STR_SHORT_DASH,  StrId::STR_MEDIUM_DASH,
                                          StrId::STR_LONG_DASH,  StrId::STR_DOTTED_LINE, StrId::STR_WAVY_LINE};
constexpr int MARGIN_MIN = CrossPointSettings::SCREEN_MARGIN_MIN;
constexpr int MARGIN_MAX = CrossPointSettings::SCREEN_MARGIN_MAX;
constexpr int MARGIN_STEP = CrossPointSettings::SCREEN_MARGIN_STEP;
constexpr StrId OK_OPTION[] = {StrId::STR_OK_BUTTON};
}  // namespace

TextSettingsActivity::TextSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                           const SdCardFontRegistry* registry, Tab initialTab,
                                           const InitialFontState initialFontState, const StartMode startMode)
    : UiTabListActivity("TextSettings", renderer, mappedInput),
      registry_(registry),
      tab_(initialTab),
      initialFontState_(initialFontState),
      startMode_(startMode) {}

const char* TextSettingsActivity::tabLabel(const int index) const { return I18N.get(TAB_NAME_IDS[index]); }

void TextSettingsActivity::onEnter() {
  UiTabListActivity::onEnter();

  if (sdFontSystem.adoptCompleteChineseNotoSans()) initialFontState_ = InitialFontState::Changed;
  {
    RenderLock lock(*this);
    sdFontSystem.ensureLoaded(renderer, false);
  }

  metrics_ = UITheme::getInstance().getMetrics();
  afterHeader = metrics_.topPadding + metrics_.headerHeight + metrics_.verticalSpacing;
  bottomReserved = metrics_.buttonHintsHeight + metrics_.verticalSpacing;
  usableHeight = renderer.getScreenHeight() - afterHeader - bottomReserved;
  previewHeight = usableHeight * metrics_.previewHeightPercent / 100;

  fonts_.clear();
  fonts_.reserve(CrossPointSettings::BUILTIN_FONT_COUNT + (registry_ ? registry_->getFamilyCount() : 0));
#ifdef ENABLE_CHINESE_VERSION
  if (!registry_ || !registry_->findFamily(SdCardFontSystem::COMPLETE_CHINESE_NOTO_SANS_FAMILY)) {
    fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
  }
#else
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SERIF), true, static_cast<uint8_t>(CrossPointSettings::NOTOSERIF)});
  fonts_.push_back({I18N.get(StrId::STR_NOTO_SANS), true, static_cast<uint8_t>(CrossPointSettings::NOTOSANS)});
#endif
  if (registry_) {
    const auto& families = registry_->getFamilies();
    for (int i = 0; i < static_cast<int>(families.size()); i++) {
      fonts_.push_back({families[i].name, false, static_cast<uint8_t>(CrossPointSettings::BUILTIN_FONT_COUNT + i)});
    }
  }

  rebuildSizeList();

  currentFamilyIndex_ = 0;
  for (int i = 0; i < static_cast<int>(fonts_.size()); i++) {
    const auto& font = fonts_[i];
    const bool selected = font.isBuiltin
                              ? SETTINGS.sdFontFamilyName[0] == '\0' && font.settingIndex == SETTINGS.fontFamily
                              : font.name == SETTINGS.sdFontFamilyName;
    if (selected) {
      currentFamilyIndex_ = i;
      break;
    }
  }
  initialFamilyIndex_ = currentFamilyIndex_;
  initialPointSize_ = SETTINGS.fontPointSize;
  initialSdFontFlashPreload_ = SETTINGS.sdFontFlashPreload;
  // Per-tab ring positions (0 = tab bar, 1..N = row). The base reset each
  // tab's nav with followOnBuild armed, so each tab's first build shows its
  // remembered selection (Family/Size open on the current item).
  for (auto& n : tabNavs) n.selected = 1;  // default to the first list row
  tabNavs[static_cast<int>(Tab::Family)].selected = currentFamilyIndex_ + 1;
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;
  tabNavs[static_cast<int>(tab_)].selected = 0;  // screen opens with the tab bar focused, not a list row

  rebuildRowItems();
  if (startMode_ == StartMode::PreloadThenExit) exitAfterFinalFont(ExitDestination::Previous);
}

void TextSettingsActivity::onExit() { Activity::onExit(); }

// Rebuilds rowItems_ (label + actionValue) for the active tab. Structural —
// call only when tab_ or its backing data (fonts_/sizes_) changes, never from
// buildScreen(), which just refreshes rowValues_/rowItems_[].value in place.
void TextSettingsActivity::rebuildRowItems() {
  const int count = listCount();
  rowValues_.assign(count, std::string());
  rowItems_.clear();
  rowItems_.reserve(count);
  for (int i = 0; i < count; i++) {
    fui::ListItem item;
    switch (tab_) {
      case Tab::Family:
        item.label = fonts_[i].name.c_str();
        break;
      case Tab::Size:
        item.label = sizes_[i].name.c_str();
        break;
      case Tab::Layout:
        item.label = I18N.get(LAYOUT_ROW_NAME_IDS[i]);
        break;
      case Tab::Style:
        item.label = I18N.get(STYLE_ROW_NAME_IDS[static_cast<int>(styleRowAt(i))]);
        break;
      default:
        break;
    }
    item.actionValue = static_cast<int16_t>(i);
    rowItems_.push_back(item);
  }
}

// The selectable sizes belong to the active family, so this runs on entry and
// again after every family change. A family change goes through ensureLoaded(),
// which snaps SETTINGS.fontPointSize into the new family's set — but entry does
// not, so the highlight is resolved by snapping rather than by exact match.
void TextSettingsActivity::rebuildSizeList() {
  const std::vector<uint8_t> points = readerFontPointSizes(registry_, SETTINGS.sdFontFamilyName);

  // The stored size can still sit outside this family's set — e.g. the family
  // was deleted while selected, or the card was swapped. Highlight the size the
  // reader actually renders, which getReaderFontId() resolves the same way.
  const uint8_t selectedPt = snapToNearestPointSize(points, SETTINGS.fontPointSize);

  sizes_.clear();
  sizes_.reserve(points.size());
  currentSizeIndex_ = 0;
  for (const uint8_t pt : points) {
    // "pt" is deliberately not translated: it is the typographic unit symbol,
    // written the same way in every language CrossPoint ships.
    char label[12];
    snprintf(label, sizeof(label), "%u pt", pt);
    if (pt == selectedPt) currentSizeIndex_ = static_cast<int>(sizes_.size());
    sizes_.push_back({label, pt});
  }
}

void TextSettingsActivity::onTabAction(const int index) {
  if (optionPopup_.isActive()) return;
  if (tab_ != static_cast<Tab>(index)) {
    tab_ = static_cast<Tab>(index);
    rebuildRowItems();
    auto& n = activeNav();
    n.selected = 0;          // tab taps land with the tab bar focused (legacy tap behavior)
    n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
    requestUpdate();
  }
  // The switched-to tab repaints as the selected pill; a flash overlay on top
  // of it just repaints the pill in the focused style.
  app.clearTapFlash();
}

void TextSettingsActivity::activateIndex(const int index) {
  if (optionPopup_.isActive()) return;
  // Most rows repaint a different surface (popup, preview, new value);
  // a lingering tap flash would gray an unrelated element.
  app.clearTapFlash();
  activateRow(index);
}

bool TextSettingsActivity::handleCustomInput() {
  return optionPopup_.handleInput(mappedInput, [this] { requestUpdate(); });
}

bool TextSettingsActivity::handleButtons() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    exitAfterFinalFont(ExitDestination::Previous);
    return true;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    if (ringPos() == 0) {
      switchTab();
    } else {
      activateRow(ringPos() - 1);
    }
    return true;
  }

  return false;
}

void TextSettingsActivity::buildScreen(UiScreen& screen) {
  // Content sits below the preview pane (render() draws header + preview
  // directly) and above the caption band + button hints.
  const int tabTop = afterHeader + previewHeight;
  const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
  screen.setContentMargin(
      fui::Insets{static_cast<int16_t>(tabTop), 0, static_cast<int16_t>(bottomReserved + captionHeight), 0});

  buildTabBar(screen);

  // rowItems_ (label/actionValue) was built by rebuildRowItems() when the tab
  // was last switched; only the live value text needs refreshing here, by
  // assigning into the existing rowValues_ strings (no vector growth) rather
  // than building a new items/values vector on every render.
  const int count = listCount();
  for (int i = 0; i < count; i++) {
    switch (tab_) {
      case Tab::Family:
        rowValues_[i] = (i == currentFamilyIndex_) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Size:
        rowValues_[i] = (i == currentSizeIndex_) ? tr(STR_SELECTED) : "";
        break;
      case Tab::Layout:
        rowValues_[i] = layoutValueText(i);
        break;
      case Tab::Style:
        rowValues_[i] = styleValueText(i);
        break;
      default:
        break;
    }
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  props.valueInset = 8;               // air between the value and the row edge
  // Keep titles and values at the same list font size; long labels may wrap.
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncTabListViewport(screen, props);
  screen.list(props);
}

const char* TextSettingsActivity::confirmLabelText() const {
  if (ringPos() == 0) {
    // Confirm on the tab bar advances to the next tab.
    return I18N.get(TAB_NAME_IDS[(static_cast<int>(tab_) + 1) % static_cast<int>(Tab::Count)]);
  }
  switch (tab_) {
    case Tab::Layout:
      // Extra Paragraph Spacing toggles; the rest open a picker
      return ringPos() - 1 == static_cast<int>(LayoutRow::ParaSpacing) ? tr(STR_TOGGLE) : tr(STR_SELECT);
    case Tab::Style:
      if (ringPos() > 0) {
        const StyleRow row = styleRowAt(ringPos() - 1);
        if (row == StyleRow::ReadingGuideLineStyle || row == StyleRow::ReadingGuideLineOffset ||
            row == StyleRow::FakeBold) {
          return tr(STR_SELECT);
        }
      }
      return tr(STR_TOGGLE);
    default:
      return tr(STR_SELECT);
  }
}

void TextSettingsActivity::render(RenderLock&&) {
  const FontLoadState fontLoadState = fontLoadState_.load();
  if (fontLoadState != FontLoadState::Idle) {
    fontpreload::draw(renderer, preloadFamilyName_, preloadPointSize_, preloadCompleted_.load(), preloadTotal_.load(),
                      fontLoadState == FontLoadState::Ready ? fontpreload::State::Ready : fontpreload::State::Progress);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    return;
  }

  if (optionPopup_.processRender(renderer, mappedInput)) return;  // picker draws over everything

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();

  GUI.drawHeader(renderer, Rect{0, metrics_.topPadding, pageWidth, metrics_.headerHeight}, tr(STR_TEXT_SETTINGS));

  const char* familyName = (currentFamilyIndex_ >= 0 && currentFamilyIndex_ < static_cast<int>(fonts_.size()))
                               ? fonts_[currentFamilyIndex_].name.c_str()
                               : "";
  const char* sizeName = (currentSizeIndex_ >= 0 && currentSizeIndex_ < static_cast<int>(sizes_.size()))
                             ? sizes_[currentSizeIndex_].name.c_str()
                             : "";
  textsettings::renderPreview(renderer, previewLayout_, metrics_.previewPadding, metrics_.verticalSpacing, afterHeader,
                              previewHeight, familyName, sizeName);

  // Tab bar + active tab's list draw inside the screen builder.
  renderUi();

  if (focusedRowHasNoPreview()) {
    const int captionHeight = renderer.getTextHeight(UI_10_FONT_ID) + metrics_.verticalSpacing;
    const int capY = afterHeader + usableHeight - captionHeight + metrics_.verticalSpacing;
    renderer.drawText(UI_10_FONT_ID, metrics_.previewPadding, capY, tr(STR_NOT_IN_PREVIEW));
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabelText(), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}

// Font switching runs on the main task from loop(), which deliberately holds no
// RenderLock. ensureLoaded() deletes the resident SdCardFont before loading the
// next one, and the render task walks that same object inside the preview's
// prewarmCache() — so without this lock a font switch can free the mini glyph
// arrays out from under prewarmStyle() (crash: null s.miniGlyphs mid-read/sort).
void TextSettingsActivity::applyFamily(int listIndex) {
  RenderLock lock;
  const auto& font = fonts_[listIndex];
  if (font.isBuiltin) {
    SETTINGS.fontFamily = font.settingIndex;
    SETTINGS.sdFontFamilyName[0] = '\0';
    SETTINGS.sdFontFlashPreload = 0;
    sdFontSystem.ensureLoaded(renderer);  // unloads the previously resident SD font
    currentFamilyIndex_ = listIndex;
  } else if (registry_) {
    const int sdIdx = font.settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
    const auto& families = registry_->getFamilies();
    if (sdIdx < static_cast<int>(families.size())) {
      strncpy(SETTINGS.sdFontFamilyName, families[sdIdx].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
      SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      SETTINGS.sdFontFlashPreload = 0;
      sdFontSystem.ensureLoaded(renderer);
      currentFamilyIndex_ = listIndex;
    }
  }

  if (currentFamilyIndex_ != listIndex) return;  // switch failed — keep the old size list

  // The new family ships its own set of point sizes, and ensureLoaded() may have
  // snapped the selection into it, so the Size tab's list and its nav position
  // both have to be rebuilt.
  rebuildSizeList();
  tabNavs[static_cast<int>(Tab::Size)].selected = currentSizeIndex_ + 1;
}

void TextSettingsActivity::activateRow(int row) {
  switch (tab_) {
    case Tab::Family:
      if (row != currentFamilyIndex_) {
        applyFamily(row);
        // Persist immediately (like SettingsActivity's per-change saves): the
        // parent's result callback only runs on a normal finish(), so relying
        // on it loses the change when this screen is left via the home
        // gesture/key or a sleep. Saved here, not inside applyFamily, so the
        // SD write happens outside its RenderLock.
        if (currentFamilyIndex_ == row) {
          SETTINGS.saveToFile();
        }
#ifdef ENABLE_CHINESE_VERSION
        maybeOfferCompleteChineseFont();
#endif
        requestUpdate();
      }
      break;
    case Tab::Size:
      if (row != currentSizeIndex_) {
        applySize(row);
        SETTINGS.saveToFile();
#ifdef ENABLE_CHINESE_VERSION
        maybeOfferCompleteChineseFont();
#endif
        requestUpdate();
      }
      break;
    case Tab::Layout:
      confirmLayoutRow(row);
      break;
    case Tab::Style:
      confirmStyleRow(row);
      break;
    default:
      break;
  }
}

// Same RenderLock rationale as applyFamily(): a size change reloads the SD font
// file, which frees and replaces the SdCardFont the render task may be reading.
void TextSettingsActivity::applySize(int listIndex) {
  RenderLock lock;

  currentSizeIndex_ = listIndex;
  SETTINGS.fontPointSize = sizes_[listIndex].pointSize;
  if (SETTINGS.sdFontFamilyName[0] != '\0') SETTINGS.sdFontFlashPreload = 0;
  sdFontSystem.ensureLoaded(renderer);
}

const SdCardFontFileInfo* TextSettingsActivity::fontFileForFamily(const int listIndex, const uint8_t pointSize) const {
  if (!registry_ || listIndex < 0 || listIndex >= static_cast<int>(fonts_.size()) || fonts_[listIndex].isBuiltin) {
    return nullptr;
  }
  const int familyIndex = fonts_[listIndex].settingIndex - CrossPointSettings::BUILTIN_FONT_COUNT;
  const auto& families = registry_->getFamilies();
  return familyIndex >= 0 && familyIndex < static_cast<int>(families.size())
             ? families[familyIndex].findNearestSize(pointSize)
             : nullptr;
}

bool TextSettingsActivity::preloadFont(const SdCardFontFileInfo& file, const char* familyName) {
  size_t cachedPayloadSize = 0;
  const bool alreadyCached = SdCardFontCache::isValidFor(file.path.c_str(), &cachedPayloadSize);
  {
    RenderLock lock(*this);
    preloadFamilyName_ = familyName;
    preloadPointSize_ = file.pointSize;
    preloadVerifying_ = false;
    preloadCompleted_.store(alreadyCached ? cachedPayloadSize * 2 : 0);
    preloadTotal_.store(alreadyCached ? cachedPayloadSize * 2 : 1);
    lastPreloadPercent_ = 0;
    fontLoadState_.store(FontLoadState::Preloading);
    if (!alreadyCached) sdFontSystem.releaseLoadedFont(renderer);
  }
  requestUpdateAndWait();

  if (alreadyCached) {
    {
      RenderLock lock(*this);
      fontLoadState_.store(FontLoadState::Ready);
    }
    requestUpdateAndWait();
    return true;
  }

  const auto result = SdCardFontCache::preload(
      file.path.c_str(),
      [](size_t completed, size_t total, void* context) {
        auto* self = static_cast<TextSettingsActivity*>(context);
        self->preloadTotal_.store(total);
        self->preloadCompleted_.store(completed);
        const bool verifying = completed > total / 2;
        const bool phaseChanged = self->preloadVerifying_ != verifying;
        self->preloadVerifying_ = verifying;
        const unsigned percent = total > 0 ? static_cast<unsigned>(completed * 100 / total) : 0;
        if (phaseChanged || percent == 100 || percent >= self->lastPreloadPercent_ + 10) {
          self->lastPreloadPercent_ = percent;
          self->requestUpdate(true);
        }
      },
      this);

  const bool succeeded = result == SdCardFontCache::Result::Ok || result == SdCardFontCache::Result::AlreadyCached;
  if (succeeded) {
    requestUpdateAndWait();
    {
      RenderLock lock(*this);
      fontLoadState_.store(FontLoadState::Ready);
    }
    requestUpdateAndWait();
  }
  return succeeded;
}

void TextSettingsActivity::exitAfterFinalFont(const ExitDestination destination) {
  if (exitInProgress_) return;
  exitInProgress_ = true;
  exitDestination_ = destination;

  const bool fontChanged = initialFontState_ == InitialFontState::Changed ||
                           currentFamilyIndex_ != initialFamilyIndex_ || SETTINGS.fontPointSize != initialPointSize_;
  if (!fontChanged) {
    SETTINGS.sdFontFlashPreload = initialSdFontFlashPreload_;
    SETTINGS.saveToFile();
    completeExit();
    return;
  }

  if (SETTINGS.sdFontFamilyName[0] == '\0') {
    SETTINGS.sdFontFlashPreload = 0;
    SETTINGS.saveToFile();
    completeExit();
    return;
  }

  SETTINGS.sdFontFlashPreload = 1;
  SETTINGS.saveToFile();
  const auto* file = fontFileForFamily(currentFamilyIndex_, SETTINGS.fontPointSize);
  const bool succeeded = file && preloadFont(*file, SETTINGS.sdFontFamilyName);
  {
    RenderLock lock(*this);
    fontLoadState_.store(FontLoadState::Idle);
    sdFontSystem.ensureLoaded(renderer, succeeded);
  }
  if (succeeded) {
    completeExit();
    return;
  }

  SETTINGS.sdFontFlashPreload = 0;
  SETTINGS.saveToFile();
  exitInProgress_ = false;
  // Preload failure is informational (the font still loads from SD at runtime);
  // acknowledging the popup exits exactly like the success path, with a
  // cancelled result so the caller does not treat it as a font change.
  ActivityResult result;
  result.isCancelled = true;
  setResult(std::move(result));
  optionPopup_.show(StrId::STR_FONT_PRELOAD_FAILED, OK_OPTION, static_cast<int>(std::size(OK_OPTION)), 0,
                    [this](int) { completeExit(); });
  requestUpdate();
}

void TextSettingsActivity::completeExit() {
  if (exitDestination_ == ExitDestination::Home) {
    onGoHome();
  } else {
    finish();
  }
}

bool TextSettingsActivity::handleHomeGesture() {
  exitAfterFinalFont(ExitDestination::Home);
  return true;
}

#ifdef ENABLE_CHINESE_VERSION
void TextSettingsActivity::maybeOfferCompleteChineseFont() {
  if (FontDownloadActivity::wasChineseFontPromptShownThisBoot() || SETTINGS.sdFontFamilyName[0] != '\0' ||
      SETTINGS.fontPointSize < 14) {
    return;
  }

  SETTINGS.saveToFile();
  auto downloader =
      makeUniqueNoThrow<FontDownloadActivity>(renderer, mappedInput, FontDownloadActivity::Purpose::PromptThenManage);
  if (!downloader) {
    LOG_ERR("FONT", "OOM allocating FontDownloadActivity (%zu bytes)", sizeof(FontDownloadActivity));
    return;
  }
  startActivityForResult(std::move(downloader), [this](const ActivityResult&) { requestUpdate(); });
}
#endif

void TextSettingsActivity::confirmLayoutRow(int row) {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::ParaSpacing:
      SETTINGS.extraParagraphSpacing = !SETTINGS.extraParagraphSpacing;
      SETTINGS.saveToFile();
      requestUpdate();
      break;
    case LayoutRow::LineSpacing:
      optionPopup_.show(StrId::STR_LINE_SPACING, LINE_SPACING_IDS, static_cast<int>(std::size(LINE_SPACING_IDS)),
                        SETTINGS.lineSpacing, [](int idx) {
                          SETTINGS.lineSpacing = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::Alignment:
      optionPopup_.show(StrId::STR_ALIGNMENT, ALIGNMENT_IDS, static_cast<int>(std::size(ALIGNMENT_IDS)),
                        SETTINGS.paragraphAlignment, [](int idx) {
                          SETTINGS.paragraphAlignment = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      break;
    case LayoutRow::ScreenMargin: {
      std::vector<std::string> options;
      options.reserve((MARGIN_MAX - MARGIN_MIN) / MARGIN_STEP + 1);
      for (int m = MARGIN_MIN; m <= MARGIN_MAX; m += MARGIN_STEP) options.push_back(std::to_string(m));
      const int cur = (std::clamp<int>(SETTINGS.screenMargin, MARGIN_MIN, MARGIN_MAX) - MARGIN_MIN) / MARGIN_STEP;
      optionPopup_.show(StrId::STR_SCREEN_MARGIN, options, cur, [](int idx) {
        SETTINGS.screenMargin = static_cast<uint8_t>(MARGIN_MIN + idx * MARGIN_STEP);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      break;
    }

    default:
      break;
  }
}

std::string TextSettingsActivity::layoutValueText(int row) const {
  switch (static_cast<LayoutRow>(row)) {
    case LayoutRow::LineSpacing: {
      const uint8_t v = SETTINGS.lineSpacing;
      return v < std::size(LINE_SPACING_IDS) ? I18N.get(LINE_SPACING_IDS[v]) : I18N.get(StrId::STR_NORMAL);
    }
    case LayoutRow::ParaSpacing:
      return SETTINGS.extraParagraphSpacing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case LayoutRow::Alignment: {
      const uint8_t v = SETTINGS.paragraphAlignment;
      return v < std::size(ALIGNMENT_IDS) ? I18N.get(ALIGNMENT_IDS[v]) : I18N.get(StrId::STR_JUSTIFY);
    }
    case LayoutRow::ScreenMargin:
      return std::to_string(SETTINGS.screenMargin);

    default:
      return "";
  }
}

void TextSettingsActivity::confirmStyleRow(int row) {
  switch (styleRowAt(row)) {
    case StyleRow::FocusReading:
      SETTINGS.focusReadingEnabled = !SETTINGS.focusReadingEnabled;
      break;
    case StyleRow::ReadingGuideLine:
      SETTINGS.readingGuideLineEnabled = !SETTINGS.readingGuideLineEnabled;
      rebuildRowItems();
      activeNav().selected = std::min(activeNav().selected, listCount());
      break;
    case StyleRow::ReadingGuideLineStyle:
      optionPopup_.show(StrId::STR_READING_GUIDE_LINE_STYLE, GUIDE_LINE_STYLE_IDS,
                        static_cast<int>(std::size(GUIDE_LINE_STYLE_IDS)), SETTINGS.readingGuideLineStyle, [](int idx) {
                          SETTINGS.readingGuideLineStyle = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      return;
    case StyleRow::ReadingGuideLineOffset:
      startActivityForResultWith<IntervalSelectionActivity>(
          [this](const ActivityResult& result) {
            if (!result.isCancelled) {
              const int value = std::get<IntervalResult>(result.data).value;
              SETTINGS.readingGuideLineOffset = static_cast<int8_t>(
                  std::clamp(value, static_cast<int>(CrossPointSettings::READING_GUIDE_LINE_OFFSET_MIN),
                             static_cast<int>(CrossPointSettings::READING_GUIDE_LINE_OFFSET_MAX)));
              SETTINGS.saveToFile();
            }
            requestUpdate();
          },
          "ReadingGuideLineOffset", StrId::STR_READING_GUIDE_LINE_OFFSET, SETTINGS.readingGuideLineOffset,
          CrossPointSettings::READING_GUIDE_LINE_OFFSET_MIN, CrossPointSettings::READING_GUIDE_LINE_OFFSET_MAX, 1, 5,
          StrId::STR_NONE_OPT, false);
      return;
    case StyleRow::Hyphenation:
      SETTINGS.hyphenationEnabled = !SETTINGS.hyphenationEnabled;
      break;
    case StyleRow::EmbeddedStyle:
      SETTINGS.embeddedStyle = !SETTINGS.embeddedStyle;
      break;
    case StyleRow::FakeBold:
      optionPopup_.show(StrId::STR_FAKE_BOLD, SYNTHETIC_BOLD_IDS, static_cast<int>(std::size(SYNTHETIC_BOLD_IDS)),
                        SETTINGS.fakeBold, [](int idx) {
                          SETTINGS.fakeBold = static_cast<uint8_t>(idx);
                          SETTINGS.saveToFile();
                        });
      requestUpdate();
      return;
    case StyleRow::AntiAliasing:
      SETTINGS.textAntiAliasing = !SETTINGS.textAntiAliasing;
      break;

    default:
      return;
  }
  SETTINGS.saveToFile();
  requestUpdate();
}

std::string TextSettingsActivity::styleValueText(int row) const {
  switch (styleRowAt(row)) {
    case StyleRow::FocusReading:
      return SETTINGS.focusReadingEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::ReadingGuideLine:
      return SETTINGS.readingGuideLineEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::ReadingGuideLineStyle: {
      const uint8_t style = SETTINGS.readingGuideLineStyle;
      return style < std::size(GUIDE_LINE_STYLE_IDS) ? I18N.get(GUIDE_LINE_STYLE_IDS[style])
                                                     : I18N.get(StrId::STR_SHORT_DASH);
    }
    case StyleRow::ReadingGuideLineOffset:
      return std::to_string(SETTINGS.readingGuideLineOffset);
    case StyleRow::Hyphenation:
      return SETTINGS.hyphenationEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::EmbeddedStyle:
      return SETTINGS.embeddedStyle ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    case StyleRow::FakeBold: {
      const uint8_t value = SETTINGS.fakeBold;
      return value < std::size(SYNTHETIC_BOLD_IDS) ? I18N.get(SYNTHETIC_BOLD_IDS[value]) : tr(STR_STATE_OFF);
    }
    case StyleRow::AntiAliasing:
      return SETTINGS.textAntiAliasing ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);

    default:
      return "";
  }
}

bool TextSettingsActivity::focusedRowHasNoPreview() const {
  if (ringPos() == 0 || tab_ != Tab::Style) return false;
  const StyleRow row = styleRowAt(ringPos() - 1);
  return row == StyleRow::Hyphenation || row == StyleRow::EmbeddedStyle || row == StyleRow::AntiAliasing;
}

TextSettingsActivity::StyleRow TextSettingsActivity::styleRowAt(int visibleIndex) const {
  if (visibleIndex < 0 || visibleIndex >= styleRowCount()) return StyleRow::Count;
  if (!SETTINGS.readingGuideLineEnabled && visibleIndex >= static_cast<int>(StyleRow::ReadingGuideLineStyle)) {
    visibleIndex += HIDDEN_GUIDE_ROW_COUNT;
  }
  return static_cast<StyleRow>(visibleIndex);
}

int TextSettingsActivity::styleRowCount() const {
  return static_cast<int>(StyleRow::Count) - (SETTINGS.readingGuideLineEnabled ? 0 : HIDDEN_GUIDE_ROW_COUNT);
}

void TextSettingsActivity::switchTab(const int direction) {
  const bool onTabBar = ringPos() == 0;
  constexpr int count = static_cast<int>(Tab::Count);
  tab_ = static_cast<Tab>((static_cast<int>(tab_) + direction + count) % count);
  rebuildRowItems();
  auto& n = activeNav();
  if (onTabBar) n.selected = 0;
  n.followOnBuild = true;  // pull the new tab's viewport to its remembered selection
  requestUpdate();
}

int TextSettingsActivity::listCount() const {
  switch (tab_) {
    case Tab::Family:
      return static_cast<int>(fonts_.size());
    case Tab::Size:
      return static_cast<int>(sizes_.size());
    case Tab::Layout:
      return static_cast<int>(LayoutRow::Count);
    case Tab::Style:
      return styleRowCount();

    default:
      return 0;
  }
}
