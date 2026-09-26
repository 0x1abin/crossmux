#include "VoiceNotesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <Logging.h>

#include <cstdio>

#include "VoiceNotesPlayActivity.h"
#include "VoiceNotesRecordActivity.h"
#include "VoiceNotesTranscribeActivity.h"
#include "components/UITheme.h"
#include "components/UiAppHelpers.h"

namespace fui = freeink::ui;

void VoiceNotesActivity::onEnter() {
  UiListActivity::onEnter();
  actionPending_ = false;
  popupRecording_ = -1;
  reload();
}

void VoiceNotesActivity::onExit() {
  popup_.dismiss();
  rowItems_.clear();
  rowItems_.shrink_to_fit();
  durations_.clear();
  durations_.shrink_to_fit();
  recordings_.clear();
  recordings_.shrink_to_fit();
  Activity::onExit();
}

void VoiceNotesActivity::reload() {
  RenderLock lock(*this);
  if (!voicenotes::list(recordings_)) LOG_ERR("VN", "Cannot read %s", voicenotes::DIR);

  durations_.clear();
  durations_.reserve(recordings_.size());
  rowItems_.clear();
  rowItems_.reserve(recordings_.size() + 1);

  fui::ListItem newItem;
  newItem.label = tr(STR_VOICE_NOTES_NEW);
  newItem.subtitle = tr(STR_VOICE_NOTES_MAX_LENGTH);
  newItem.actionValue = 0;
  rowItems_.push_back(newItem);

  char duration[16];
  for (const auto& rec : recordings_) {
    const uint32_t seconds = voicenotes::durationSeconds(rec.bytes);
    snprintf(duration, sizeof(duration), "%u:%02u", static_cast<unsigned>(seconds / 60),
             static_cast<unsigned>(seconds % 60));
    durations_.emplace_back(duration);
  }
  for (size_t i = 0; i < recordings_.size(); ++i) {
    fui::ListItem item;
    item.label = recordings_[i].name;
    item.value = durations_[i].c_str();
    item.subtitle =
        recordings_[i].hasTranscript ? tr(STR_VOICE_NOTES_HAS_TRANSCRIPT) : tr(STR_VOICE_NOTES_NO_TRANSCRIPT);
    item.actionValue = static_cast<int16_t>(rowItems_.size());
    rowItems_.push_back(item);
  }

  if (nav.selected >= listCount()) nav.selected = listCount() - 1;
  if (nav.selected < 0) nav.selected = 0;
  nav.follow(listCount());
}

void VoiceNotesActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const Rect safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
  screen.setContentMarginFromScreen(fui::Insets{
      static_cast<int16_t>(safe.y + metrics.topPadding + metrics.headerHeight),
      static_cast<int16_t>(renderer.getScreenWidth() - (safe.x + safe.width)),
      static_cast<int16_t>(renderer.getScreenHeight() - (safe.y + safe.height)), static_cast<int16_t>(safe.x)});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  fui::ListProps props;
  props.items = rowItems_.data();
  props.count = static_cast<uint16_t>(rowItems_.size());
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;  // physical buttons stay in loop()
  syncListViewport(screen, props, /*hasSubtitle=*/true);
  screen.list(props);
}

bool VoiceNotesActivity::handleCustomInput() {
  if (popup_.handleInput(mappedInput, [this] { requestUpdate(); })) return true;
  if (actionPending_) {
    actionPending_ = false;
    runAction(pendingAction_, popupRecording_);
    return true;
  }
  return false;
}

void VoiceNotesActivity::activateIndex(const int index) {
  if (popup_.isActive() || actionPending_ || index < 0 || index >= listCount()) return;
  app.clearTapFlash();
  nav.selected = index;
  if (index == 0) {
    startRecording();
    return;
  }
  showActions(index - 1);
}

void VoiceNotesActivity::startRecording() {
  startActivityForResultWith<VoiceNotesRecordActivity>([this](const ActivityResult&) {
    reload();
    // The new recording is the newest, i.e. the first row after "New".
    if (listCount() > 1) nav.selected = 1;
    nav.follow(listCount());
  });
}

void VoiceNotesActivity::showActions(const int recordingIndex) {
  if (recordingIndex < 0 || recordingIndex >= static_cast<int>(recordings_.size())) return;
  const auto& rec = recordings_[recordingIndex];

  const char* labels[4];
  int count = 0;
#if CROSSPOINT_CAP_SOUND_FEEDBACK
  popupActions_[count] = Action::Play;
  labels[count++] = tr(STR_VOICE_NOTES_PLAY);
#endif
  const int transcribeIndex = count;
  popupActions_[count] = Action::Transcribe;
  labels[count++] = tr(STR_VOICE_NOTES_TRANSCRIBE);
  if (rec.hasTranscript) {
    popupActions_[count] = Action::ViewTranscript;
    labels[count++] = tr(STR_VOICE_NOTES_VIEW_TRANSCRIPT);
  }
  popupActions_[count] = Action::Delete;
  labels[count++] = tr(STR_DELETE);

  popupRecording_ = recordingIndex;
  // A recording with a transcript most often gets reopened to read it.
  const int initial = rec.hasTranscript ? transcribeIndex + 1 : 0;
  const int optionCount = count;
  popup_.show(rec.name, labels, count, initial, [this, optionCount](const int choice) {
    if (choice < 0 || choice >= optionCount) return;
    pendingAction_ = popupActions_[choice];
    actionPending_ = true;
  });
  requestUpdate();
}

void VoiceNotesActivity::runAction(const Action action, const int recordingIndex) {
  if (recordingIndex < 0 || recordingIndex >= static_cast<int>(recordings_.size())) return;
  const char* name = recordings_[recordingIndex].name;
  switch (action) {
    case Action::Play:
      startActivityForResultWith<VoiceNotesPlayActivity>([this](const ActivityResult&) { requestUpdate(); }, name);
      break;
    case Action::Transcribe:
      startActivityForResultWith<VoiceNotesTranscribeActivity>([this](const ActivityResult&) { reload(); }, name);
      break;
    case Action::ViewTranscript: {
      char path[voicenotes::PATH_LEN];
      voicenotes::txtPath(name, path, sizeof(path));
      activityManager.goToReader(path);
      break;
    }
    case Action::Delete:
      confirmDelete(recordingIndex);
      break;
  }
}

void VoiceNotesActivity::confirmDelete(const int recordingIndex) {
  const char* options[] = {tr(STR_CANCEL), tr(STR_DELETE)};
  popupRecording_ = recordingIndex;
  popup_.show(tr(STR_VOICE_NOTES_DELETE_CONFIRM), options, 2, 0, [this](const int choice) {
    if (choice != 1 || popupRecording_ < 0 || popupRecording_ >= static_cast<int>(recordings_.size())) return;
    if (!voicenotes::remove(recordings_[popupRecording_].name)) {
      LOG_ERR("VN", "Delete failed: %s", recordings_[popupRecording_].name);
    }
    reload();
  });
  requestUpdate();
}

void VoiceNotesActivity::render(RenderLock&&) {
  renderer.clearScreen();
  drawChrome();
  renderUi();
  for (int pass = 0; nav.consumeRebuildNeeded() && pass < 8; ++pass) {
    renderer.clearScreen();
    drawChrome();
    renderUi();
  }
  if (popup_.processRender(renderer, mappedInput)) return;
  drawFooter();
  renderer.displayBuffer();
}
