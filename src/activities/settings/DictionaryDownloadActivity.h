#pragma once

#include <I18n.h>

#include <cstdint>
#include <string>
#include <vector>

#include "activities/Activity.h"
#include "components/UiAppHost.h"
#include "util/ButtonNavigator.h"
#include "util/DictionaryResource.h"

class DictionaryDownloadActivity final : public Activity, private UiAppHost {
 public:
  DictionaryDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool skipLoopDelay() override { return true; }
  bool preventAutoSleep() override {
    // The synchronous downloader blocks the manager's normal polling while it
    // runs, so keep the next observable state awake as well.
    return state_ == State::Loading || state_ == State::Downloading || state_ == State::Complete ||
           state_ == State::Error;
  }

 private:
  enum class State : uint8_t { WifiSelection, Loading, List, Downloading, Complete, Error };
  enum class Operation : uint8_t { None, Single, UpdateAll };
  enum class DownloadResult : uint8_t { Success, Cancelled, Failed };

  struct ManifestFile {
    std::string name;
    size_t size = 0;
    uint32_t crc32 = 0;
  };

  struct ManifestItem {
    std::string id;
    std::string name;
    std::string description;
    int revision = 0;
    std::vector<ManifestFile> files;
    size_t totalSize = 0;
    DictionaryResource::LocalState localState = DictionaryResource::LocalState::NotInstalled;
  };

  State state_ = State::WifiSelection;
  Operation operation_ = Operation::None;
  ButtonNavigator buttonNavigator_;
  std::string baseUrl_;
  std::vector<ManifestItem> items_;
  int selectedIndex_ = 0;
  int downloadingIndex_ = -1;
  size_t currentFileIndex_ = 0;
  size_t currentFileTotal_ = 0;
  size_t fileProgress_ = 0;
  size_t fileTotal_ = 0;
  StrId errorMessage_ = StrId::STR_DOWNLOAD_FAILED;
  bool cancelRequested_ = false;
  bool waitForConfirmRelease_ = false;
  bool waitForBackRelease_ = false;
  bool goHomeRequested_ = false;

  static constexpr freeink::ui::ActionId ACTION_CANCEL_DOWNLOAD = 1;
  static constexpr freeink::ui::ActionId ACTION_RETURN_TO_LIST = 2;
  static constexpr freeink::ui::ActionId ACTION_RETRY_DOWNLOAD = 3;
  static void stateScreen(UiScreen& screen, void* user);
  static void onCancelDownload(const freeink::ui::ActionEvent& event, void* user);
  static void onReturnToList(const freeink::ui::ActionEvent& event, void* user);
  static void onRetryDownload(const freeink::ui::ActionEvent& event, void* user);
  void buildStateScreen(UiScreen& screen);
  void returnToList();

  void startWifiSelection();
  void onWifiSelectionComplete(bool success);
  bool fetchAndParseManifest();
  void recoverInstall(const char* id);
  void refreshLocalState(ManifestItem& item);
  DownloadResult downloadItem(ManifestItem& item, bool selectAfterInstall);
  void downloadSingle(int itemIndex);
  void updateAll();
  void retryOperation();
  void promptDeleteSelected();
  void onDeleteConfirmationResult(const ActivityResult& result);
  bool writeMarker(const char* stagePath, int revision);
  bool commitInstall(const char* id);
  static bool validateDownloadedFile(const char* path, const ManifestFile& file);
  static bool ifoUses64BitOffsets(const char* path);
  static std::string formatSize(size_t bytes);
  bool showUpdateAllRow() const;
  int itemIndexFromList(int listIndex) const { return listIndex - (showUpdateAllRow() ? 1 : 0); }
  int listItemCount() const { return static_cast<int>(items_.size()) + (showUpdateAllRow() ? 1 : 0); }
  bool isUpdateAllRow(int index) const { return showUpdateAllRow() && index == 0; }
};
