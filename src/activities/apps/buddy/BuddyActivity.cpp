// Pixel buddy portraits adapted from claude-desktop-buddy.
// Copyright 2026 Anthropic, PBC.
// SPDX-License-Identifier: MIT

#include "BuddyActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <HalSystem.h>
#include <I18n.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>

#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {

constexpr uint32_t kRevealFrameHoldMs = 1500;
constexpr uint32_t kIdleFrameHoldMs = 1500;
constexpr int kSpriteRows = 5;
constexpr int kCardBodyOffsetY = -10;
constexpr int kIdleLiftPx = 2;

struct Sprite {
  const char* rows[kSpriteRows];
};

// '?' marks an eye slot and is replaced at render time. The compact, static
// portraits preserve the reference project's idle silhouettes without runtime
// asset decoding or a second framebuffer.
constexpr Sprite kSprites[] = {
    {{"            ", "    __      ", "  <(? )___  ", "   (  ._>   ", "    `--'    "}},      // Duck
    {{"            ", "    (?>     ", "    ||      ", "  _(__)_    ", "   ^^^^     "}},      // Goose
    {{"            ", "   .----.   ", "  ( ?  ? )  ", "  (      )  ", "   `----'   "}},      // Blob
    {{"            ", "   /\\_/\\    ", "  ( ?   ? ) ", "  (  w   )  ", "  (\")_(\")   "}},  // Cat
    {{"            ", "  /^\\  /^\\  ", " <  ?    ? >", " (   ww   ) ", "  `-vvvv-'  "}},    // Dragon
    {{"            ", "   .----.   ", "  ( ?  ? )  ", "  (______)  ", "  /\\/\\/\\/\\  "}},  // Octopus
    {{"            ", "   /\\  /\\   ", "  ((?)(?))  ", "  (  ><  )  ", "   `----'   "}},    // Owl
    {{"   .---.    ", "  ( ?>? )   ", " /(     )\\  ", "  `-----'   ", "   J   L    "}},     // Penguin
    {{"            ", "   _,--._   ", "  ( ?    ?) ", " /[______]\\ ", "  ``    ``  "}},     // Turtle
    {{"  \\\\  /     ", "    .--.    ", "  _( ?? )_  ", " (___@@___) ", "  ~~~~~~~~  "}},    // Snail
    {{"            ", "   .----.   ", "  ( ?    ? )", "  |   __   |", "  ~`~``~`~  "}},      // Ghost
    {{"            ", "}~(______)~{", "}~( ?  ? )~{", "  ( .--. )  ", "  (_/  \\_)  "}},     // Axolotl
    {{"            ", "  n______n  ", " ( ?    ? ) ", " (   oo   ) ", "  `------'  "}},      // Capybara
    {{"            ", " n  ____  n ", " | |?  ?| | ", " |_|    |_| ", "   |    |   "}},      // Cactus
    {{"            ", "   .[||].   ", "  [ ?    ? ]", "  [ ==== ]  ", "  `------'  "}},      // Robot
    {{"    (\\_/)   ", "   ( ? ? )  ", "  =(  v  )= ", "   (\")_(\")  ", "            "}},   // Rabbit
    {{"            ", " .-o-OO-o-. ", "(__________)", "   |?   ?|  ", "   |____|   "}},      // Mushroom
    {{"            ", "  /\\____/\\  ", " ( ?    ? ) ", " (   ..   ) ", "  `------'  "}},    // Chonk
};

constexpr char kEyeGlyphs[] = {'.', '*', 'x', 'O', '@', 'o'};
constexpr const char* kHatRows[] = {
    "", "    /\\     ", "   [___]    ", "   _/|\\_    ", "   (---)    ", "    /\\     ", "   (___)    ", "    <(')    ",
};

constexpr StrId kRarityLabels[] = {
    StrId::STR_BUDDY_RARITY_COMMON, StrId::STR_BUDDY_RARITY_UNCOMMON,  StrId::STR_BUDDY_RARITY_RARE,
    StrId::STR_BUDDY_RARITY_EPIC,   StrId::STR_BUDDY_RARITY_LEGENDARY,
};

constexpr StrId kSpeciesLabels[] = {
    StrId::STR_BUDDY_SPECIES_DUCK,     StrId::STR_BUDDY_SPECIES_GOOSE,    StrId::STR_BUDDY_SPECIES_BLOB,
    StrId::STR_BUDDY_SPECIES_CAT,      StrId::STR_BUDDY_SPECIES_DRAGON,   StrId::STR_BUDDY_SPECIES_OCTOPUS,
    StrId::STR_BUDDY_SPECIES_OWL,      StrId::STR_BUDDY_SPECIES_PENGUIN,  StrId::STR_BUDDY_SPECIES_TURTLE,
    StrId::STR_BUDDY_SPECIES_SNAIL,    StrId::STR_BUDDY_SPECIES_GHOST,    StrId::STR_BUDDY_SPECIES_AXOLOTL,
    StrId::STR_BUDDY_SPECIES_CAPYBARA, StrId::STR_BUDDY_SPECIES_CACTUS,   StrId::STR_BUDDY_SPECIES_ROBOT,
    StrId::STR_BUDDY_SPECIES_RABBIT,   StrId::STR_BUDDY_SPECIES_MUSHROOM, StrId::STR_BUDDY_SPECIES_CHONK,
};

constexpr StrId kStatLabels[] = {
    StrId::STR_BUDDY_STAT_FOCUS,  StrId::STR_BUDDY_STAT_PATIENCE, StrId::STR_BUDDY_STAT_LUCK,
    StrId::STR_BUDDY_STAT_WISDOM, StrId::STR_BUDDY_STAT_COURAGE,
};

static_assert(std::size(kSprites) == static_cast<size_t>(buddy::Species::Count));
static_assert(std::size(kEyeGlyphs) == static_cast<size_t>(buddy::Eye::Count));
static_assert(std::size(kHatRows) == static_cast<size_t>(buddy::Hat::Count));
static_assert(std::size(kRarityLabels) == static_cast<size_t>(buddy::Rarity::Count));
static_assert(std::size(kSpeciesLabels) == static_cast<size_t>(buddy::Species::Count));
static_assert(std::size(kStatLabels) == static_cast<size_t>(buddy::Stat::Count));

Rect buddyContentBounds(const GfxRenderer& renderer, const bool hasFrontButtonHints = false) {
  auto& theme = UITheme::getInstance();
  const auto& metrics = theme.getMetrics();
  const Rect safe = theme.getScreenSafeArea(renderer, hasFrontButtonHints, false);
  const int horizontalInset = std::max(8, metrics.contentSidePadding / 2);
  const int verticalInset = std::max(6, metrics.verticalSpacing / 2);
  return Rect{safe.x + horizontalInset, safe.y + verticalInset, std::max(1, safe.width - horizontalInset * 2),
              std::max(1, safe.height - verticalInset * 2)};
}

void drawCentered(const GfxRenderer& renderer, const int fontId, const Rect& bounds, const int y, const char* text,
                  const EpdFontFamily::Style style = EpdFontFamily::REGULAR) {
  const int width = renderer.getTextWidth(fontId, text, style);
  renderer.drawText(fontId, bounds.x + (bounds.width - width) / 2, y, text, true, style);
}

void drawCenteredGridRow(const GfxRenderer& renderer, const int fontId, const Rect& bounds, const int y,
                         const char* text) {
  const size_t length = strlen(text);
  const int cellWidth = renderer.getTextWidth(fontId, "M", EpdFontFamily::BOLD);
  const int x = bounds.x + (bounds.width - static_cast<int>(length) * cellWidth) / 2;
  for (size_t i = 0; i < length; ++i) {
    if (text[i] == ' ') continue;
    const char glyph[] = {text[i], '\0'};
    renderer.drawText(fontId, x + static_cast<int>(i) * cellWidth, y, glyph, true, EpdFontFamily::BOLD);
  }
}

void drawPixelStar(const GfxRenderer& renderer, const int x, const int y, const int scale = 1) {
  renderer.fillRect(x + 3 * scale, y, scale, 7 * scale);
  renderer.fillRect(x, y + 3 * scale, 7 * scale, scale);
  renderer.fillRect(x + scale, y + scale, scale, scale);
  renderer.fillRect(x + 5 * scale, y + scale, scale, scale);
  renderer.fillRect(x + scale, y + 5 * scale, scale, scale);
  renderer.fillRect(x + 5 * scale, y + 5 * scale, scale, scale);
}

void drawEgg(const GfxRenderer& renderer, const int centerX, const int top, const int scale, const bool cracked) {
  const int unit = std::max(2, scale);
  renderer.fillRect(centerX - 4 * unit, top, 8 * unit, unit);
  renderer.fillRect(centerX - 6 * unit, top + unit, 12 * unit, 2 * unit);
  renderer.fillRect(centerX - 7 * unit, top + 3 * unit, 14 * unit, 4 * unit);
  renderer.fillRect(centerX - 6 * unit, top + 7 * unit, 12 * unit, 2 * unit);
  renderer.fillRect(centerX - 4 * unit, top + 9 * unit, 8 * unit, unit);
  if (cracked) {
    renderer.drawLine(centerX - unit, top + unit, centerX + unit, top + 4 * unit, unit, false);
    renderer.drawLine(centerX + unit, top + 4 * unit, centerX - 2 * unit, top + 6 * unit, unit, false);
    renderer.drawLine(centerX - 2 * unit, top + 6 * unit, centerX + unit, top + 9 * unit, unit, false);
  }
}

void drawSilhouette(const GfxRenderer& renderer, const Rect& bounds) {
  const int cx = bounds.x + bounds.width / 2;
  const int cy = bounds.y + bounds.height / 2;
  const int unit = std::max(3, std::min(bounds.width, bounds.height) / 30);
  renderer.fillRect(cx - 8 * unit, cy - 5 * unit, 16 * unit, 12 * unit);
  renderer.fillRect(cx - 6 * unit, cy - 8 * unit, 12 * unit, 3 * unit);
  renderer.fillRect(cx - 7 * unit, cy + 7 * unit, 4 * unit, 3 * unit);
  renderer.fillRect(cx + 3 * unit, cy + 7 * unit, 4 * unit, 3 * unit);
}

void drawSprite(const GfxRenderer& renderer, const Rect& bounds, const buddy::Traits& traits, const bool blinking,
                const int yOffset) {
  const int fontId = bounds.width >= 250 && bounds.height >= 150 ? NOTOSANS_18_FONT_ID : UI_12_FONT_ID;
  const int lineHeight = renderer.getLineHeight(fontId);
  const int hatRows = traits.hat == buddy::Hat::None ? 0 : 1;
  const int totalHeight = (kSpriteRows + hatRows) * lineHeight;
  int y = bounds.y + std::max(0, (bounds.height - totalHeight) / 2) + yOffset;

  if (hatRows != 0) {
    drawCenteredGridRow(renderer, fontId, bounds, y, kHatRows[static_cast<size_t>(traits.hat)]);
    y += lineHeight;
  }

  const Sprite& sprite = kSprites[static_cast<size_t>(traits.species)];
  const char eye = blinking ? '-' : kEyeGlyphs[static_cast<size_t>(traits.eye)];
  for (const char* source : sprite.rows) {
    char row[16];
    snprintf(row, sizeof(row), "%s", source);
    for (char* ch = row; *ch != '\0'; ++ch) {
      if (*ch == '?') *ch = eye;
    }
    drawCenteredGridRow(renderer, fontId, bounds, y, row);
    y += lineHeight;
  }

  if (traits.shiny) {
    drawPixelStar(renderer, bounds.x + bounds.width - 28, bounds.y + 8);
    drawPixelStar(renderer, bounds.x + bounds.width - 20, bounds.y + bounds.height / 3);
    drawPixelStar(renderer, bounds.x + 18, bounds.y + bounds.height - 22);
  }
}

void drawStatRows(const GfxRenderer& renderer, const Rect& bounds, const buddy::Traits& traits) {
  constexpr int rowCount = static_cast<int>(buddy::Stat::Count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int rowHeight = std::max(lineHeight, metrics.progressBarHeight);
  const int rowGap = std::max(6, metrics.verticalSpacing / 2);
  const int columnGap = std::max(8, metrics.contentSidePadding / 2);
  int labelWidth = 0;
  for (const StrId label : kStatLabels) {
    // cppcheck-suppress useStlAlgorithm
    labelWidth = std::max(labelWidth, renderer.getTextWidth(UI_10_FONT_ID, I18N.get(label)));
  }
  const int valueColumnWidth = renderer.getTextWidth(UI_10_FONT_ID, "100", EpdFontFamily::BOLD);
  const int progressX = bounds.x + labelWidth + columnGap;
  const int valueColumnX = bounds.x + bounds.width - valueColumnWidth;
  const int progressWidth = std::max(1, valueColumnX - columnGap - progressX);

  for (int i = 0; i < rowCount; ++i) {
    const int rowY = bounds.y + i * (rowHeight + rowGap);
    const int textY = rowY + (rowHeight - lineHeight) / 2;
    const int progressY = rowY + (rowHeight - metrics.progressBarHeight) / 2;
    const uint8_t value = traits.stats[static_cast<size_t>(i)];
    const char* label = I18N.get(kStatLabels[i]);
    const int labelX = bounds.x + labelWidth - renderer.getTextWidth(UI_10_FONT_ID, label);
    char number[4];
    snprintf(number, sizeof(number), "%u", static_cast<unsigned>(value));
    const int numberWidth = renderer.getTextWidth(UI_10_FONT_ID, number, EpdFontFamily::BOLD);
    const int numberX = bounds.x + bounds.width - numberWidth;

    renderer.drawText(UI_10_FONT_ID, labelX, textY, label);
    GUI.drawProgressBar(renderer, Rect{progressX, progressY, progressWidth, metrics.progressBarHeight}, value, 100,
                        false);
    renderer.drawText(UI_10_FONT_ID, numberX, textY, number, true, EpdFontFamily::BOLD);
  }
}

int statRowsHeight(const GfxRenderer& renderer) {
  constexpr int rowCount = static_cast<int>(buddy::Stat::Count);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int rowHeight = std::max(renderer.getLineHeight(UI_10_FONT_ID), metrics.progressBarHeight);
  const int rowGap = std::max(6, metrics.verticalSpacing / 2);
  return rowCount * rowHeight + (rowCount - 1) * rowGap;
}

}  // namespace

void BuddyActivity::onEnter() {
  Activity::onEnter();
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);

  HalSystem::DeviceId deviceId{};
  if (!HalSystem::getDeviceId(deviceId)) {
    stage_ = Stage::Error;
    requestUpdate();
    return;
  }

  traits_ = buddy::generate(deviceId);
  if (SETTINGS.buddyClaimed != 0) {
    stage_ = Stage::Card;
    requestUpdate();
    return;
  }

  stage_ = Stage::WaitingForClaim;
  requestUpdateAndWait();
}

void BuddyActivity::loop() {
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    activityManager.goToApps();
    return;
  }

  switch (stage_) {
    case Stage::WaitingForClaim: {
      int touchX = 0;
      int touchY = 0;
      if (mappedInput.wasScreenTapped(touchX, touchY)) {
        advanceReveal();
        break;
      }
    }
      if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        advanceReveal();
      }
      break;
    case Stage::Crack:
    case Stage::Burst:
      if (static_cast<int32_t>(millis() - nextFrameAt_) >= 0) {
        advanceReveal();
      }
      break;
    case Stage::RevealCard:
      break;
    case Stage::Card:
      if (cardRendered_ && static_cast<int32_t>(millis() - nextFrameAt_) >= 0) {
        advanceIdleFrame();
      }
      break;
    case Stage::Error:
      break;
  }
}

void BuddyActivity::advanceReveal() {
  switch (stage_) {
    case Stage::WaitingForClaim:
      stage_ = Stage::Crack;
      break;
    case Stage::Crack:
      stage_ = Stage::Burst;
      break;
    case Stage::Burst:
      stage_ = Stage::RevealCard;
      break;
    case Stage::RevealCard:
    case Stage::Card:
    case Stage::Error:
      return;
  }

  requestUpdateAndWait();
  if (stage_ == Stage::RevealCard) {
    finishClaim();
  } else {
    nextFrameAt_ = millis() + kRevealFrameHoldMs;
  }
}

void BuddyActivity::advanceIdleFrame() {
  switch (idleFrame_) {
    case IdleFrame::RestBeforeBlink:
      idleFrame_ = IdleFrame::Blink;
      break;
    case IdleFrame::Blink:
      idleFrame_ = IdleFrame::RestBeforeLift;
      break;
    case IdleFrame::RestBeforeLift:
      idleFrame_ = IdleFrame::Lift;
      break;
    case IdleFrame::Lift:
      idleFrame_ = IdleFrame::RestBeforeBlink;
      break;
  }

  requestUpdate();
}

void BuddyActivity::finishClaim() {
  stage_ = Stage::Card;
  nextFrameAt_ = millis() + kIdleFrameHoldMs;
  SETTINGS.buddyClaimed = 1;
  if (!SETTINGS.saveToFile()) {
    SETTINGS.buddyClaimed = 0;
    LOG_ERR("BUDDY", "Failed to save claimed state");
  }
}

void BuddyActivity::render(RenderLock&&) {
  renderer.setOrientation(GfxRenderer::Orientation::Portrait);
  switch (stage_) {
    case Stage::WaitingForClaim:
    case Stage::Crack:
    case Stage::Burst:
      drawRevealFrame();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
    case Stage::RevealCard:
    case Stage::Card:
      drawCard();
      renderer.displayBuffer(stage_ == Stage::RevealCard ? HalDisplay::HALF_REFRESH : HalDisplay::FAST_REFRESH);
      if (SETTINGS.textAntiAliasing && !cardRendered_) {
        renderer.clearScreen(0x00);
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_LSB);
        drawCard(false);
        renderer.copyGrayscaleLsbBuffers();

        renderer.clearScreen(0x00);
        renderer.setRenderMode(GfxRenderer::GRAYSCALE_MSB);
        drawCard(false);
        renderer.copyGrayscaleMsbBuffers();

        renderer.displayGrayBuffer();
        renderer.setRenderMode(GfxRenderer::BW);
        drawCard();
        renderer.cleanupGrayscaleWithFrameBuffer();
      }
      if (stage_ == Stage::Card) nextFrameAt_ = millis() + kIdleFrameHoldMs;
      cardRendered_ = true;
      break;
    case Stage::Error:
      drawError();
      renderer.displayBuffer(HalDisplay::FAST_REFRESH);
      break;
  }
}

void BuddyActivity::drawRevealFrame() {
  renderer.clearScreen();
  const bool awaitingClaim = stage_ == Stage::WaitingForClaim;
  const Rect content = buddyContentBounds(renderer, awaitingClaim);
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int centerX = content.x + content.width / 2;
  const int eggScale = std::max(3, std::min(content.width, content.height) / 45);
  const int eggTop = content.y + std::max(20, content.height / 5);

  switch (stage_) {
    case Stage::WaitingForClaim:
      drawEgg(renderer, centerX, eggTop, eggScale, false);
      {
        const int mysteryY = eggTop + eggScale * 12;
        drawCentered(renderer, NOTOSANS_18_FONT_ID, content, mysteryY, tr(STR_BUDDY_MYSTERY), EpdFontFamily::BOLD);
        drawCentered(renderer, UI_10_FONT_ID, content,
                     mysteryY + renderer.getLineHeight(NOTOSANS_18_FONT_ID) + metrics.verticalSpacing,
                     I18n::getInstance().get(mappedInput.hasTouch() ? StrId::STR_BUDDY_TOUCH_CLAIM_PROMPT
                                                                    : StrId::STR_BUDDY_CLAIM_PROMPT),
                     EpdFontFamily::BOLD);
        const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_CONFIRM), "", "");
        GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
      }
      break;
    case Stage::Crack:
      drawEgg(renderer, centerX, eggTop, eggScale, true);
      drawPixelStar(renderer, centerX - eggScale * 9, eggTop + eggScale * 3, 2);
      drawPixelStar(renderer, centerX + eggScale * 7, eggTop + eggScale * 6);
      drawCentered(renderer, NOTOSANS_18_FONT_ID, content, eggTop + eggScale * 12, tr(STR_BUDDY_MYSTERY),
                   EpdFontFamily::BOLD);
      break;
    case Stage::Burst:
      drawSilhouette(renderer, Rect{content.x, content.y + 20, content.width, content.height - 50});
      drawPixelStar(renderer, content.x + content.width / 6, content.y + content.height / 4, 2);
      drawPixelStar(renderer, content.x + content.width * 4 / 5, content.y + content.height / 5);
      drawPixelStar(renderer, content.x + content.width / 4, content.y + content.height * 3 / 4);
      drawPixelStar(renderer, content.x + content.width * 3 / 4, content.y + content.height * 2 / 3, 2);
      drawCentered(renderer, UI_12_FONT_ID, content,
                   content.y + content.height - renderer.getLineHeight(UI_12_FONT_ID) - 8, tr(STR_BUDDY_REVEAL),
                   EpdFontFamily::BOLD);
      break;
    case Stage::RevealCard:
    case Stage::Card:
    case Stage::Error:
      break;
  }
}

void BuddyActivity::drawCard(const bool clear) {
  if (clear) renderer.clearScreen();
  const Rect card = buddyContentBounds(renderer);
  const auto& metrics = UITheme::getInstance().getMetrics();

  const int textX = card.x + 8;
  const int titleY = card.y + 10;
  const StrId rarityId = kRarityLabels[static_cast<size_t>(traits_.rarity)];
  const StrId speciesId = kSpeciesLabels[static_cast<size_t>(traits_.species)];
  renderer.drawText(NOTOSERIF_18_FONT_ID, textX, titleY, I18N.get(speciesId), true, EpdFontFamily::BOLD);

  const int metaY = titleY + renderer.getLineHeight(NOTOSERIF_18_FONT_ID) + 2;
  char meta[48];
  snprintf(meta, sizeof(meta), "%s · %s", traits_.name.data(), I18N.get(rarityId));
  renderer.drawText(UI_10_FONT_ID, textX, metaY, meta);

  const int rarityStars = static_cast<int>(traits_.rarity) + 1;
  const int starsX = textX + 3;
  const int starsY = metaY + renderer.getLineHeight(UI_10_FONT_ID) + metrics.verticalSpacing / 2 + 5;
  for (int i = 0; i < rarityStars; ++i) {
    drawPixelStar(renderer, starsX + i * 20, starsY, 2);
  }

  if (traits_.shiny) {
    const char* shiny = tr(STR_BUDDY_SHINY);
    const int width = renderer.getTextWidth(UI_10_FONT_ID, shiny, EpdFontFamily::BOLD);
    const int shinyX = card.x + card.width - width - 4;
    drawPixelStar(renderer, shinyX - 14, titleY + 3);
    renderer.drawText(UI_10_FONT_ID, shinyX, titleY, shiny, true, EpdFontFamily::BOLD);
  }

  const int bodyTop = starsY + 12 + kCardBodyOffsetY;
  const int statsInset = std::max(12, card.width * 8 / 100);
  const int statsHeight = statRowsHeight(renderer);
  const int statsY = card.y + card.height - statsHeight - metrics.verticalSpacing * 3 + kCardBodyOffsetY;
  const Rect art{card.x, bodyTop, card.width, std::max(1, statsY - bodyTop - 8)};
  const Rect stats{card.x + statsInset, statsY, card.width - statsInset * 2, statsHeight};
  const bool blinking = idleFrame_ == IdleFrame::Blink;
  const int spriteYOffset = idleFrame_ == IdleFrame::Lift ? -kIdleLiftPx : 0;
  drawSprite(renderer, art, traits_, blinking, spriteYOffset);
  drawStatRows(renderer, stats, traits_);
}

void BuddyActivity::drawError() {
  renderer.clearScreen();
  UITheme::drawCenteredWrappedText(renderer, buddyContentBounds(renderer), UI_12_FONT_ID, tr(STR_BUDDY_DEVICE_ID_ERROR),
                                   3);
}
