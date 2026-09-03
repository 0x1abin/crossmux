#include "ShengBeiActivity.h"

#include <Arduino.h>
#include <I18n.h>
#include <HalTiltSensor.h>
#include <time.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <cmath>
#include <algorithm>

#include "components/UITheme.h"
#include "components/icons/shengbei_bitmaps.h"
#include "fontIds.h"
#include "util/TimeUtils.h"
#include "I18nKeys.h"

namespace {

constexpr int kLayoutLeft = 24;
constexpr int kLayoutRight = 504;  // 528 - 24

inline void drawRule(GfxRenderer& renderer, int left, int right, int y) {
  renderer.drawLine(left, y, right - 1, y, /*state=*/true);
}

// 针对中英文混排的 UTF-8 字符级安全换行绘制
int drawWrappedTextCjk(const GfxRenderer& renderer, int fontId, int left, int right, int startY, int lineSpacing,
                       const char* text, int maxLines = 3, bool alignLeft = true) {
  if (!text || *text == '\0') return 0;

  const int maxWidth = right - left;
  const int fontH = renderer.getTextHeight(fontId);
  const int stepY = fontH + lineSpacing;

  std::string currentLine;
  int currentY = startY;
  int linesDrawn = 0;

  const char* p = text;
  while (*p != '\0' && linesDrawn < maxLines) {
    int charLen = 1;
    const unsigned char c = static_cast<unsigned char>(*p);
    if ((c & 0x80) == 0) {
      charLen = 1;
    } else if ((c & 0xE0) == 0xC0) {
      charLen = 2;
    } else if ((c & 0xF0) == 0xE0) {
      charLen = 3;
    } else if ((c & 0xF8) == 0xF0) {
      charLen = 4;
    }

    std::string candidate = currentLine;
    candidate.append(p, charLen);

    if (renderer.getTextWidth(fontId, candidate.c_str()) <= maxWidth) {
      currentLine = std::move(candidate);
      p += charLen;
    } else {
      if (!currentLine.empty()) {
        const int lineW = renderer.getTextWidth(fontId, currentLine.c_str());
        const int lineX = alignLeft ? left : (left + (maxWidth - lineW) / 2);
        renderer.drawText(fontId, lineX, currentY, currentLine.c_str(), /*black=*/true);
        currentY += stepY;
        linesDrawn++;
        currentLine.clear();
      } else {
        currentLine.append(p, charLen);
        p += charLen;
      }
    }
  }

  if (!currentLine.empty() && linesDrawn < maxLines) {
    const int lineW = renderer.getTextWidth(fontId, currentLine.c_str());
    const int lineX = alignLeft ? left : (left + (maxWidth - lineW) / 2);
    renderer.drawText(fontId, lineX, currentY, currentLine.c_str(), /*black=*/true);
    linesDrawn++;
  }

  return linesDrawn;
}

// 1:1 绘制基于用户原图生成的 1-bit 高清圣杯位图
void drawCupBitmap(const GfxRenderer& renderer, int x, int y, bool isFlat, bool isFlipped) {
  const uint8_t* bits = isFlat ? (isFlipped ? shengbei_icons::CupFlatRight_bits : shengbei_icons::CupFlatLeft_bits)
                               : (isFlipped ? shengbei_icons::CupConvexRight_bits : shengbei_icons::CupConvexLeft_bits);
  constexpr int w = shengbei_icons::CupFlatLeft_width;
  constexpr int h = shengbei_icons::CupFlatLeft_height;
  constexpr int rowBytes = (w + 7) / 8;

  for (int r = 0; r < h; ++r) {
    for (int c = 0; c < w; ++c) {
      const uint8_t byte = bits[r * rowBytes + c / 8];
      if (((byte >> (7 - c % 8)) & 1U) != 0) {
        renderer.drawPixel(x + c, y + r, /*state=*/true);
      }
    }
  }
}

}  // namespace

ShengBeiActivity::ShengBeiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("ShengBei", renderer, mappedInput) {}

void ShengBeiActivity::onEnter() {
  Activity::onEnter();
  halTiltSensor.wake();
  viewState_ = ViewState::Guide;
  throwCount_ = 0;
  consecutiveSheng_ = 0;
  requestUpdate();
}

void ShengBeiActivity::onExit() {
  halTiltSensor.deepSleep();
  Activity::onExit();
}

void ShengBeiActivity::triggerCast() {
  // 如果是从引导页开始掷杯，次数重新从 0 开始计算
  if (viewState_ == ViewState::Guide) {
    throwCount_ = 0;
    consecutiveSheng_ = 0;
  }
  viewState_ = ViewState::Shaking;
  stateChangeMs_ = millis();
  requestUpdate();
}

void ShengBeiActivity::loop() {
  // 返回 / 重问 键处理 (KEY 1)
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (viewState_ == ViewState::Result) {
      // 从结果页重问：重置次数并返回引导页
      throwCount_ = 0;
      consecutiveSheng_ = 0;
      viewState_ = ViewState::Guide;
      requestUpdate();
    } else if (viewState_ == ViewState::Guide) {
      activityManager.goToApps();
    }
    return;
  }

  // 动效状态过渡
  if (viewState_ == ViewState::Shaking) {
    if (millis() - stateChangeMs_ >= 350) {
      currentResult_ = shengbei::castOracle();
      throwCount_++;
      if (currentResult_.outcome == shengbei::Outcome::ShengBei) {
        consecutiveSheng_++;
      } else {
        consecutiveSheng_ = 0;
      }
      viewState_ = ViewState::Result;
      requestUpdate();
    }
    return;
  }

  // 确认键 (KEY 2) 或 晃动传感器触发
  const bool shaken = halTiltSensor.pollShake();
  const bool confirmed = mappedInput.wasReleased(MappedInputManager::Button::Confirm);

  if (shaken || confirmed) {
    triggerCast();
    return;
  }
}

void ShengBeiActivity::drawPageFrame(const Rect& viewport) {
  constexpr int kOuterInset = 6;
  constexpr int kInnerInset = 11;
  renderer.drawRect(viewport.x + kOuterInset, viewport.y + kOuterInset, viewport.width - 2 * kOuterInset,
                    viewport.height - 2 * kOuterInset, /*lineWidth=*/2, /*state=*/true);
  renderer.drawRect(viewport.x + kInnerInset, viewport.y + kInnerInset, viewport.width - 2 * kInnerInset,
                    viewport.height - 2 * kInnerInset, /*lineWidth=*/1, /*state=*/true);

  // 四角折线点缀
  constexpr int kCornerOffset = 15;
  constexpr int kCornerLen = 8;
  renderer.drawLine(viewport.x + kCornerOffset, viewport.y + kCornerOffset,
                    viewport.x + kCornerOffset + kCornerLen, viewport.y + kCornerOffset, true);
  renderer.drawLine(viewport.x + kCornerOffset, viewport.y + kCornerOffset,
                    viewport.x + kCornerOffset, viewport.y + kCornerOffset + kCornerLen, true);

  renderer.drawLine(viewport.x + viewport.width - kCornerOffset - kCornerLen, viewport.y + kCornerOffset,
                    viewport.x + viewport.width - kCornerOffset, viewport.y + kCornerOffset, true);
  renderer.drawLine(viewport.x + viewport.width - kCornerOffset, viewport.y + kCornerOffset,
                    viewport.x + viewport.width - kCornerOffset, viewport.y + kCornerOffset + kCornerLen, true);
}

void ShengBeiActivity::drawTopBar(int vy, int left, int right) {
  const int topY = vy + 26;
  constexpr int barH = 38;
  const int volY = topY + (barH - renderer.getTextHeight(SMALL_FONT_ID)) / 2 + 1;

  // 引导页左上角留空；仅在过场或结果页显示章节引语
  if (viewState_ != ViewState::Guide) {
    const char* volName = (viewState_ == ViewState::Shaking) ? "感通 · 天地交泰" : "所求 · 圣意昭示";
    renderer.drawText(SMALL_FONT_ID, left + 4, volY, volName, /*black=*/true);
  }

  const char* kLatinTitle = tr(STR_SHENGBEI_TITLE);
  const int latinW = renderer.getTextWidth(UI_10_FONT_ID, kLatinTitle);
  renderer.drawText(UI_10_FONT_ID, left + (right - left - latinW) / 2, volY - 1, kLatinTitle, /*black=*/true);

  // 时钟与电量
  struct tm now;
  char timeBuf[8] = "--:--";
  const uint32_t ts = TimeUtils::getCurrentValidTimestamp();
  if (ts && TimeUtils::getLocalDateTime(ts, now)) {
    std::snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", now.tm_hour, now.tm_min);
  }

  constexpr int kBatteryW = 18;
  constexpr int kBatteryH = 10;
  const int batteryX = right - 4 - kBatteryW;
  const int batteryY = topY + (barH - kBatteryH) / 2;
  const int timeW = renderer.getTextWidth(SMALL_FONT_ID, timeBuf);
  const int timeY = topY + (barH - renderer.getTextHeight(SMALL_FONT_ID)) / 2 + 1;
  renderer.drawText(SMALL_FONT_ID, batteryX - timeW - 8, timeY, timeBuf, /*black=*/true);

  const Rect batteryRect{batteryX, batteryY, kBatteryW, kBatteryH};
  GUI.drawBatteryRight(renderer, batteryRect, /*showpercentage=*/false);

  drawRule(renderer, left, right, topY + barH);
}

// 引导页：主标为“心怀所惑 · 诚心掷杯”（16pt 加粗居中），三步规则文案字号放大
void ShengBeiActivity::drawGuideView(int vy, int left, int right) {
  const int midX = left + (right - left) / 2;

  // 1. 引导页主标：“心怀所惑 · 诚心掷杯”（16pt 加粗居中）
  const int heroTop = vy + 196;
  const char* kMainText = tr(STR_SHENGBEI_GUIDE_SUB);
  const int titleW = renderer.getTextWidth(NOTOSANS_16_FONT_ID, kMainText, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSANS_16_FONT_ID, midX - titleW / 2, heroTop, kMainText, /*black=*/true, EpdFontFamily::BOLD);

  // 2. 规则清单（字号放大，排版更宽敞大气）
  const int listTop = heroTop + 68;
  drawRule(renderer, left + 20, right - 20, listTop);

  const auto drawRuleItem = [&](int y, const char* label, const char* desc) {
    const int labelFont = UI_12_FONT_ID;
    const int descFont = UI_10_FONT_ID;
    const int labelH = renderer.getTextHeight(labelFont);
    const int descH = renderer.getTextHeight(descFont);
    const int descY = y + (labelH - descH) / 2;  // 严格基线平行对齐

    renderer.drawText(labelFont, left + 20, y, label, /*black=*/true, EpdFontFamily::BOLD);
    renderer.drawText(descFont, left + 152, descY, desc, /*black=*/true);
  };

  drawRuleItem(listTop + 24, tr(STR_SHENGBEI_STEP1_TITLE), tr(STR_SHENGBEI_STEP1_DESC));
  drawRuleItem(listTop + 96, tr(STR_SHENGBEI_STEP2_TITLE), tr(STR_SHENGBEI_STEP2_DESC));
  drawRuleItem(listTop + 168, tr(STR_SHENGBEI_STEP3_TITLE), tr(STR_SHENGBEI_STEP3_DESC));

  drawRule(renderer, left + 20, right - 20, listTop + 236);
}

void ShengBeiActivity::drawShakingView(int vy, int left, int right) {
  const int midX = left + (right - left) / 2;
  const int midY = vy + 380;

  constexpr const char* kAnimText = "灵杯翻飞 · 正在落定...";
  const int textW = renderer.getTextWidth(UI_12_FONT_ID, kAnimText, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, midX - textW / 2, midY - 20, kAnimText, /*black=*/true, EpdFontFamily::BOLD);

  constexpr const char* kSubText = "感通天地，请稍候";
  const int subW = renderer.getTextWidth(UI_10_FONT_ID, kSubText);
  renderer.drawText(UI_10_FONT_ID, midX - subW / 2, midY + 22, kSubText, /*black=*/true);
}

void ShengBeiActivity::drawResultView(int vy, int left, int right) {
  const int midX = left + (right - left) / 2;

  // 1. 顶部小引
  const int ritualTop = vy + 72;
  const char* kRitual = tr(STR_SHENGBEI_RESULT_HEADER);
  const int ritW = renderer.getTextWidth(UI_10_FONT_ID, kRitual);
  renderer.drawText(UI_10_FONT_ID, midX - ritW / 2, ritualTop, kRitual, /*black=*/true);

  // 2. 双月牙圣杯上下错落展示区 (使用用户原图 1:1 生成的高清位图)
  const int cupZoneTop = ritualTop + 28;
  const int cup1X = midX - 84;
  const int cup1Y = cupZoneTop + 12;
  const int cup2X = midX + 8;
  const int cup2Y = cupZoneTop + 36;

  drawCupBitmap(renderer, cup1X, cup1Y, currentResult_.leftFlat, /*isFlipped=*/false);
  drawCupBitmap(renderer, cup2X, cup2Y, currentResult_.rightFlat, /*isFlipped=*/true);

  drawRule(renderer, left + 20, right - 20, cupZoneTop + 148);

  // 3. 卦名与核心断语 Hero 区 (18pt 大字加粗卦名，12pt 核心断语)
  const int heroTop = cupZoneTop + 164;
  const char* outcomeName = (currentResult_.outcome == shengbei::Outcome::ShengBei)
                                ? tr(STR_SHENGBEI_OUTCOME_SHENG)
                                : ((currentResult_.outcome == shengbei::Outcome::XiaoBei)
                                       ? tr(STR_SHENGBEI_OUTCOME_XIAO)
                                       : tr(STR_SHENGBEI_OUTCOME_YIN));

  const int nameW = renderer.getTextWidth(NOTOSERIF_18_FONT_ID, outcomeName, EpdFontFamily::BOLD);
  renderer.drawText(NOTOSERIF_18_FONT_ID, midX - nameW / 2, heroTop, outcomeName, /*black=*/true, EpdFontFamily::BOLD);

  const char* outcomeTag = (currentResult_.outcome == shengbei::Outcome::ShengBei)
                               ? tr(STR_SHENGBEI_TAG_SHENG)
                               : ((currentResult_.outcome == shengbei::Outcome::XiaoBei)
                                      ? tr(STR_SHENGBEI_TAG_XIAO)
                                      : tr(STR_SHENGBEI_TAG_YIN));

  const int tagW = renderer.getTextWidth(UI_12_FONT_ID, outcomeTag, EpdFontFamily::BOLD);
  renderer.drawText(UI_12_FONT_ID, midX - tagW / 2, heroTop + 66, outcomeTag, /*black=*/true, EpdFontFamily::BOLD);

  drawRule(renderer, left + 20, right - 20, heroTop + 106);

  // 4. 禅意释怀解签 (12pt 字号正文，宽敞舒适)
  const int poemTop = heroTop + 126;
  const int quoteLeft = left + 24;
  constexpr int lineSpacing = 12;
  const int fontH = renderer.getTextHeight(UI_12_FONT_ID);
  const int stepY = fontH + lineSpacing;

  const int linesDrawn = drawWrappedTextCjk(renderer, UI_12_FONT_ID, quoteLeft + 16, right - 24, poemTop,
                                           /*lineSpacing=*/lineSpacing, currentResult_.poem, /*maxLines=*/3,
                                           /*alignLeft=*/true);

  const int barH = std::max(28, linesDrawn * stepY - lineSpacing + 4);
  renderer.fillRect(quoteLeft, poemTop - 2, 3, barH, /*state=*/true);

  // 5. 底部贴底统计：字号升级为 10pt (UI_10_FONT_ID)
  constexpr int statsY = 698;
  char statsBuf[64];
  if (consecutiveSheng_ > 1) {
    std::snprintf(statsBuf, sizeof(statsBuf), "第 %d 掷  ·  ✦ 连续圣杯 %d 次 ✦", throwCount_, consecutiveSheng_);
  } else if (consecutiveSheng_ == 1) {
    std::snprintf(statsBuf, sizeof(statsBuf), "第 %d 掷  ·  连续圣杯 1 次", throwCount_);
  } else {
    std::snprintf(statsBuf, sizeof(statsBuf), "第 %d 掷", throwCount_);
  }
  const int statW = renderer.getTextWidth(UI_10_FONT_ID, statsBuf);
  renderer.drawText(UI_10_FONT_ID, midX - statW / 2, statsY, statsBuf, /*black=*/true);
}

// 底部严格适配 4 物理按键（返回、确认、上一页、下一页），前两格显示提示，后两格留空
void ShengBeiActivity::drawButtonHintsBar(int vy, int left, int right) {
  const char* btn1 = (viewState_ == ViewState::Guide) ? "返回" : "重问";
  const char* btn2 = (viewState_ == ViewState::Guide) ? "掷杯" : "再掷";

  constexpr int kHintBarTop = 738;
  constexpr int kHintBarH = 34;

  drawRule(renderer, left, right, kHintBarTop);

  const int blockW = (right - left) / 4;
  const char* labels[2] = {btn1, btn2};

  for (int i = 0; i < 2; ++i) {
    if (i > 0) {
      renderer.drawLine(left + i * blockW, kHintBarTop + 6, left + i * blockW, kHintBarTop + kHintBarH - 6,
                        /*state=*/true);
    }
    const int labelW = renderer.getTextWidth(SMALL_FONT_ID, labels[i]);
    const int labelX = left + i * blockW + (blockW - labelW) / 2;
    const int labelY = kHintBarTop + (kHintBarH - renderer.getTextHeight(SMALL_FONT_ID)) / 2 + 1;
    renderer.drawText(SMALL_FONT_ID, labelX, labelY, labels[i], /*black=*/true);
  }

  // 第二格右侧绘制封闭分割线
  renderer.drawLine(left + 2 * blockW, kHintBarTop + 6, left + 2 * blockW, kHintBarTop + kHintBarH - 6,
                    /*state=*/true);
}

void ShengBeiActivity::render(RenderLock&&) {
  const int sw = renderer.getScreenWidth();
  const int sh = renderer.getScreenHeight();
  const Rect viewport{0, 0, sw, sh};

  renderer.clearScreen();

  drawPageFrame(viewport);
  drawTopBar(viewport.y, kLayoutLeft, kLayoutRight);

  if (viewState_ == ViewState::Guide) {
    drawGuideView(viewport.y, kLayoutLeft, kLayoutRight);
  } else if (viewState_ == ViewState::Shaking) {
    drawShakingView(viewport.y, kLayoutLeft, kLayoutRight);
  } else {
    drawResultView(viewport.y, kLayoutLeft, kLayoutRight);
  }

  drawButtonHintsBar(viewport.y, kLayoutLeft, kLayoutRight);

  renderer.displayBuffer();
}
