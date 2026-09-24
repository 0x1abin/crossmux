#pragma once

#include <string>
#include <vector>

#include "VoiceNotesStore.h"
#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

// Voice Notes app: row 0 starts a recording; each recording row opens an
// action popup (transcribe, view transcript, delete).
class VoiceNotesActivity final : public UiListActivity {
 public:
  explicit VoiceNotesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : UiListActivity("VoiceNotes", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void render(RenderLock&&) override;

 private:
  enum class Action : uint8_t { Transcribe, ViewTranscript, Delete };

  int listCount() const override { return static_cast<int>(rowItems_.size()); }
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  void onBackButton() override { activityManager.goToApps(); }
  const char* headerTitle() const override { return tr(STR_VOICE_NOTES_TITLE); }

  void reload();
  void startRecording();
  void showActions(int recordingIndex);
  void runAction(Action action, int recordingIndex);
  void confirmDelete(int recordingIndex);

  std::vector<voicenotes::Recording> recordings_;
  // Row text derived from recordings_, rebuilt only in reload() so repaints
  // never re-format strings.
  std::vector<std::string> durations_;
  std::vector<freeink::ui::ListItem> rowItems_;
  OptionPopup popup_;
  Action popupActions_[3] = {};
  int popupRecording_ = -1;
  // Popup choices run on the next loop pass: acting inside the callback could
  // replace the popup (or this Activity) while its std::function executes.
  bool actionPending_ = false;
  Action pendingAction_ = Action::Transcribe;
};
