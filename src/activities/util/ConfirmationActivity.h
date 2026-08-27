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

  static constexpr int MARGIN = 20;
  static constexpr int SPACING = 30;
  static constexpr int HEADING_FONT_ID = UI_12_FONT_ID;
  static constexpr int BODY_FONT_ID = UI_10_FONT_ID;
  static constexpr int MAX_BODY_LINES = 2;

  std::string safeHeading;
  std::string safeBody;
  // At most body.size() bytes; cached once so button-driven re-renders never wrap/allocate again.
  std::string safeBodySecondLine;
  OptionPopup confirmPopup;

 public:
  ConfirmationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const std::string& heading,
                       const std::string& body, BodyPlacement bodyPlacement = BodyPlacement::Page);

  void onEnter() override;
  void loop() override;
  void render(RenderLock&& lock) override;
};
