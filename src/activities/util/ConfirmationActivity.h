#pragma once
#include <cstdint>
#include <functional>
#include <string>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "fontIds.h"

class ConfirmationActivity : public Activity {
 public:
  enum class BodyPlacement : uint8_t { Page, PopupTitle };

 private:
  // Input data
  std::string heading;
  std::string body;
  BodyPlacement bodyPlacement;

  const int margin = 20;
  const int spacing = 30;
  const int fontId = UI_10_FONT_ID;

  std::string safeHeading;
  std::string safeBody;
  OptionPopup confirmPopup;
  int startY = 0;
  int lineHeight = 0;

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, BodyPlacement bodyPlacement = BodyPlacement::Page);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
