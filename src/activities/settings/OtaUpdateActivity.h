#pragma once

#include <array>
#include <cstdint>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/UiAppHost.h"
#include "network/OtaUpdater.h"

class OtaUpdateActivity : public Activity, private UiAppHost {
  enum class State : uint8_t {
    Ready,
    WifiSelection,
    CheckingForUpdate,
    UpdateAvailable,
    ConfirmingUpdate,
    UpdateInProgress,
    NoUpdate,
    Failed,
    Finished,
    ShuttingDown,
  };

  // Can't initialize this to 0 or the first render doesn't happen
  static constexpr unsigned int UNINITIALIZED_PERCENTAGE = 111;

  State state = State::Ready;
  OtaUpdater::Channel selectedChannel = OtaUpdater::Channel::Stable;
  int selectedReadyRow = 0;
  bool waitForConfirmRelease = false;
  unsigned int lastUpdaterPercentage = UNINITIALIZED_PERCENTAGE;
  OtaUpdater updater;
  OptionPopup updateConfirmation;
  std::array<uint8_t, ReleaseJsonParser::RELEASE_NOTE_COUNT_MAX + 1> releaseNotePageStarts{};
  uint8_t releaseNotePage = 0;
  uint8_t releaseNotePageCount = 1;
  // Optional detail line shown under the generic "Update failed" heading.
  // Points into the i18n string table (flash-resident, so no lifetime concern);
  // nullptr means no extra detail.
  const char* failedDetail = nullptr;

  void activateReadyRow();
  void beginWifiSelection();
  void onWifiSelectionComplete(bool success);
  void showUpdateConfirmation();
  void renderUpdateAvailable(const Rect& safeArea);
  void rebuildReleaseNotePages(const Rect& safeArea, int bottomInset);
  void runUpdateInstall();

  static constexpr freeink::ui::ActionId ACTION_RELEASE_PAGE = 1;
  static constexpr freeink::ui::ActionId ACTION_INSTALL_UPDATE = 2;
  static void updateScreen(UiScreen& screen, void* user);
  static void onReleasePage(const freeink::ui::ActionEvent& event, void* user);
  static void onInstallUpdate(const freeink::ui::ActionEvent& event, void* user);
  void buildUpdateScreen(UiScreen& screen);

 public:
  explicit OtaUpdateActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("OtaUpdate", renderer, mappedInput), UiAppHost(renderer), updater() {}
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override { return state == State::CheckingForUpdate || state == State::UpdateInProgress; }
  bool skipLoopDelay() override { return true; }  // Prevent power-saving mode
};
