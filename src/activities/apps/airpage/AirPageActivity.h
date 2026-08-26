#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "../../Activity.h"
#include "AirPageConnection.h"
#include "AirPageImageStore.h"
#include "components/OptionPopup.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"

struct Rect;

class AirPageActivity final : public Activity, private UiAppHost {
 public:
  explicit AirPageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AirPage", renderer, mappedInput), UiAppHost(renderer), connection_(renderer) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class Screen : uint8_t { Qr, Settings, History, Image };
  enum class Phase : uint8_t { Idle, FetchRequested, Fetching, WallpaperWriting, WallpaperNotice };
  enum class Notice : uint8_t {
    None,
    NoImage,
    InvalidImage,
    WifiRequired,
    WifiFailed,
    DownloadFailed,
    RealtimeRetrying,
    RealtimePaused,
    SettingsSaveFailed,
    WallpaperFailed,
  };
  enum class SettingRow : uint8_t { Mode, AutoWallpaper, Count };
  enum class WallpaperResult : uint8_t { None, Saved, Failed };
  enum class ImageDisplayResult : uint8_t { None, Success, Failure };
  enum class TouchAction : int16_t {
    BackToApps,
    ShowQr,
    Settings,
    Images,
    Refresh,
    SetWallpaper,
    OpenImageMenu,
  };

  bool processImageDisplayResult();
  void applyConnectionEvent(airpage::AirPageConnection::Event event);
  void clearConnectionNotice();

  void openSettings();
  void applySettingsSelection();
  void openHistory();
  void openSelectedHistoryImage();
  void openWallpaperConfirmation();
  void handleWallpaperResult(const ActivityResult& result);
  void handleRefresh();
  void queueFetch();
  void doFetch();
  void openWifiSelection(bool fetchAfterConnect);
  void handleWifiResult(const ActivityResult& result, bool fetchAfterConnect);
  bool consumeInputReleaseBarrier();
  void setAirPageScreen(Screen screen);
  void openImageMenu();
  void applyTouchAction(TouchAction action);
  void rebuildHistoryRows();
  void moveSettingsSelection(int index);
  void moveHistorySelection(int index);

  static constexpr freeink::ui::ActionId ACTION_TOUCH = 1;
  static constexpr freeink::ui::ActionId ACTION_SETTINGS_ROW = 2;
  static constexpr freeink::ui::ActionId ACTION_HISTORY_ROW = 3;
  static void touchScreen(UiScreen& screen, void* user);
  static void onTouchAction(const freeink::ui::ActionEvent& event, void* user);
  static void onSettingsRow(const freeink::ui::ActionEvent& event, void* user);
  static void onHistoryRow(const freeink::ui::ActionEvent& event, void* user);
  void buildTouchScreen(UiScreen& screen);

  void renderQr(const Rect& viewport);
  void renderStatus(const Rect& viewport, const char* msg);
  Rect contentViewport() const;
  const char* noticeText() const;
  const char* connectionText() const;
  const char* refreshActionText() const;
  const char* screenTitle() const;

  static constexpr int kSettingsRows = static_cast<int>(SettingRow::Count);

  Screen screen_ = Screen::Qr;
  Phase phase_ = Phase::Idle;
  Notice notice_ = Notice::None;
  WallpaperResult wallpaperResult_ = WallpaperResult::None;
  std::atomic<ImageDisplayResult> imageDisplayResult_{ImageDisplayResult::None};

  airpage::AirPageImageStore imageStore_;
  airpage::AirPageConnection connection_;
  airpage::SelectedImage selectedImage_;
  OptionPopup imageMenu_;
  ButtonNavigator buttonNavigator_;

  freeink::ui::ListNav settingsNav_;
  freeink::ui::ListNav historyNav_;
  freeink::ui::ListItem settingsRows_[kSettingsRows]{};
  freeink::ui::ListItem historyRows_[airpage::AirPageImageStore::kMaxHistoryEntries]{};
  char historyLabels_[airpage::AirPageImageStore::kMaxHistoryEntries][48]{};
  char historySubtitles_[airpage::AirPageImageStore::kMaxHistoryEntries][40]{};

  bool imageNeedsDisplay_ = true;
  bool waitForInputRelease_ = false;
  bool autoSleepWallpaper_ = false;
  int displayedScreenWidth_ = 0;
  int displayedScreenHeight_ = 0;
  int settingsSelection_ = 0;
  int historySelection_ = 0;

  // These short strings are required by the HTTP and QR APIs. They are built
  // once per activity lifetime and reused.
  std::string uploadUrl_;
  std::string downloadUrl_;
  std::string legacyDownloadUrl_;
};
