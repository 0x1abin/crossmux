#pragma once

#include <atomic>
#include <cstdint>
#include <string>

#include "../../Activity.h"
#include "AirPageConnection.h"
#include "AirPageImageStore.h"

struct Rect;

class AirPageActivity final : public Activity {
 public:
  explicit AirPageActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("AirPage", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;

 private:
  enum class Screen : uint8_t { Qr, Settings, History, Image };
  enum class Phase : uint8_t { Idle, FetchRequested, Fetching, WallpaperRequested, WallpaperWriting };
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
  void openWifiSelection();
  void handleWifiResult(const ActivityResult& result);
  bool consumeInputReleaseBarrier();

  void renderQr(const Rect& viewport);
  void renderStatus(const Rect& viewport, const char* msg);
  void renderSettings(const Rect& viewport);
  void renderHistory(const Rect& viewport);
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
};
