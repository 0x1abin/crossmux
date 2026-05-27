#pragma once

#include <cstdint>

#include "../../../util/ButtonNavigator.h"
#include "../../Activity.h"

class WeReadMenuActivity final : public Activity {
 public:
  WeReadMenuActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~WeReadMenuActivity() override = default;

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  int selected = 0;

  // Set in onEnter() and refreshed when the user returns from Setup.
  bool wifiOk = false;
  bool keyOk = false;

  // Sticky banner: when set, the next render() shows a popup before drawing
  // the menu. Cleared by any input.
  enum class Banner : uint8_t { None, NoWifi, ComingSoon };
  Banner banner = Banner::None;

  // Menu-item index the user picked while WiFi was off. -1 = nothing pending.
  // launchAutoConnect()'s completion handler reads this to decide whether to
  // dispatch into a sub-activity once the connection comes up.
  int pendingItemAfterConnect_ = -1;

  void refreshGates();
  void onSelect();
  void launchAutoConnect();
  void dispatchMenuItem(int itemIndex);
};
