#pragma once

#include "activities/Activity.h"
#include "ShengBeiData.h"

class ShengBeiActivity final : public Activity {
 public:
  enum class ViewState {
    Guide,    // 心流引导页
    Shaking,  // 掷杯翻飞动效/过渡态
    Result,   // 卦象揭晓页
  };

  explicit ShengBeiActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~ShengBeiActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ViewState viewState_ = ViewState::Guide;
  shengbei::ShengBeiResult currentResult_{};
  int throwCount_ = 0;
  int consecutiveSheng_ = 0;
  unsigned long stateChangeMs_ = 0;

  void triggerCast();
  void drawPageFrame(const Rect& viewport);
  void drawTopBar(int vy, int left, int right);
  void drawGuideView(int vy, int left, int right);
  void drawShakingView(int vy, int left, int right);
  void drawResultView(int vy, int left, int right);
  void drawCup(int cx, int cy, bool isFlat, bool isFlipped);
  void drawButtonHintsBar(int vy, int left, int right);
};
