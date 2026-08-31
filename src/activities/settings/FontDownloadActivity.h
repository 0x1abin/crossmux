#pragma once

#include <string>
#include <vector>

#include "FontInstaller.h"
#include "SdCardFont.h"
#include "activities/UiListActivity.h"

// JSON schema version of the fonts.json manifest. The canonical version for
// the build tooling lives in lib/EpdFont/scripts/cpfont_version.py. This
// firmware-side copy must be bumped manually when the firmware is updated to
// support a new manifest schema.
#define FONTS_MANIFEST_VERSION 1

#define FONT_MANIFEST_URL_STRINGIFY_INNER(x) #x
#define FONT_MANIFEST_URL_STRINGIFY(x) FONT_MANIFEST_URL_STRINGIFY_INNER(x)

class FontDownloadActivity final : public UiListActivity {
 public:
  enum class Purpose : uint8_t { Manage, PromptThenManage, ReaderAutoInstall };
  enum class StartMode : uint8_t { Normal, ResumeFontLoadError };

  explicit FontDownloadActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                Purpose purpose = Purpose::Manage, StartMode startMode = StartMode::Normal);

#ifdef ENABLE_CHINESE_VERSION
  static bool wasChineseFontPromptShownThisBoot();
  static void suppressChineseFontPromptThisBoot();
#endif

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override {
    return state_ == LOADING_MANIFEST || state_ == DOWNLOADING ||
           // The download is synchronous and blocks the main loop until it
           // completes, so activityManager.preventAutoSleep() is never polled
           // during downloading.
           state_ == COMPLETE || state_ == ERROR;
  }
  bool skipLoopDelay() override { return true; }

 private:
  enum State {
    WIFI_SELECTION,
    LOADING_MANIFEST,
    GROUP_LIST,
    FAMILY_LIST,
    DOWNLOADING,
    SELECTING_FONT,
    COMPLETE,
    ERROR,
  };

  enum class DownloadOperation : uint8_t { None, Single, DownloadAll, UpdateAll };
  enum class DownloadResult : uint8_t { Success, Cancelled, Failed };
  enum class AutomaticError : uint8_t { Download, FamilyMissing, PointSizeMissing, SettingsSave, FontLoad };
  enum class ExitRoute : uint8_t { Home, Reader, ReaderSuppressPrompt, ReaderPreloadChineseFont };

  struct ManifestFile {
    size_t size = 0;
    uint32_t crc32 = 0;
    uint8_t pointSize = 0;
  };

  struct ManifestFamily {
    std::string name;
    std::string description;
    size_t fileOffset = 0;
    size_t fileCount = 0;
    size_t totalSize = 0;
    bool installed = false;
    bool hasUpdate = false;
    uint32_t scriptMask = 0;
  };

  static constexpr size_t MAX_SCRIPT_GROUPS = 32;

  State state_ = WIFI_SELECTION;
  Purpose purpose_;
  StartMode startMode_;
  FontInstaller fontInstaller_;

  // Manifest data
  std::string baseUrl_;
  std::vector<ManifestFamily> families_;
  std::vector<ManifestFile> files_;
  // Manifest-defined labels are dynamic; cap them at the 32-bit membership
  // mask and retain only labels after parsing so group tags consume no steady-state heap.
  std::vector<std::string> scriptGroupLabels_;
  // One 4-byte index per manifest family, allocated once and reused for every group.
  std::vector<int> filteredIndices_;
  freeink::ui::ListNav groupNav_;

  // Download progress
  size_t currentFileIndex_ = 0;
  size_t currentFileTotal_ = 0;
  size_t downloadProgress_ = 0;
  size_t downloadTotal_ = 0;
  uint8_t lastProgressRefreshPercent_ = 0;
  int downloadingFamilyIndex_ = -1;
  std::string errorMessage_;
  bool cancelRequested_ = false;
  DownloadOperation operation_ = DownloadOperation::None;
  bool selectionUpdated_ = false;
  bool accelerationCompleted_ = false;
  uint8_t targetPointSize_ = 0;
  AutomaticError automaticError_ = AutomaticError::Download;
  ExitRoute exitRoute_ = ExitRoute::Home;
  // Set when the cancel came from the home gesture (consumed by the download
  // callback's own input pump); exit to home after the abort unwinds.
  bool goHomeRequested_ = false;

  // Shared cache for group and family rows. It is rebuilt only when the visible
  // list changes, never for cursor movement or tap flash repaints.
  std::vector<std::string> rowLabels_;
  std::vector<freeink::ui::ListItem> rowItems_;
  bool rowsDirty_ = true;
  void rebuildRowItems();
  void rebuildGroupRowItems();
  void rebuildFamilyRowItems();

  static constexpr freeink::ui::ActionId ACTION_CANCEL_DOWNLOAD = ACTION_USER;
  static constexpr freeink::ui::ActionId ACTION_RETURN_TO_LIST = ACTION_USER + 1;
  static constexpr freeink::ui::ActionId ACTION_RETRY_DOWNLOAD = ACTION_USER + 2;
  static void onCancelDownload(const freeink::ui::ActionEvent& event, void* user);
  static void onReturnToList(const freeink::ui::ActionEvent& event, void* user);
  static void onRetryDownload(const freeink::ui::ActionEvent& event, void* user);
  void returnToFamilyList();

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  freeink::ui::ListNav& activeNav() override;
  void onBackButton() override;
  // Non-list states (loading, downloading, complete, error) consume the loop
  // pass here; the group and family lists use the base list protocol.
  bool handleCustomInput() override;

  void activateSelected();

  void startWifiSelection();
  void onWifiSelectionComplete(bool success);
  bool startAutomaticDownload();
  void finishAutomaticFlow(ExitRoute route);
  const char* automaticErrorText() const;
  bool fetchAndParseManifest();
  void resetDownloadProgress(size_t total);
  void updateDownloadProgress(size_t completed);
  void finishDownloadProgress();
  DownloadResult downloadFile(const ManifestFamily& family, const ManifestFile& file);
  DownloadResult downloadFamily(ManifestFamily& family);
  void downloadSingle(int familyIndex);
  void retryDownloadOperation();
  void selectDownloadedFontAndPreview(const char* familyName);
  void downloadAll();
  void updateAll();
  static bool computeFileCrc32(const char* path, uint32_t& outCrc);
  bool isVerifiedFontFile(const char* path, const ManifestFile& file);
  bool showDownloadAllRow() const;
  bool showUpdateAllRow() const;
  int specialRowCount() const;
  bool isDownloadAllRow(int index) const;
  bool isUpdateAllRow(int index) const;
  bool isSelectedFamilyDeletable() const;
  void promptDeleteSelectedFamily();
  void onDeleteConfirmationResult(const ActivityResult& result);
  int familyIndexFromList(int listIndex) const;
  int listItemCount() const;
  bool hasGroupScreen() const { return !scriptGroupLabels_.empty(); }
  int groupListItemCount() const { return 1 + static_cast<int>(scriptGroupLabels_.size()); }
  int groupMemberCount(int scriptGroupIndex) const;
  void buildFilteredIndices(int groupListIndex);
  void enterGroup(int groupListIndex);
  size_t totalDownloadSize() const;
  size_t totalUpdateSize() const;
  static std::string formatSize(size_t bytes);
};
