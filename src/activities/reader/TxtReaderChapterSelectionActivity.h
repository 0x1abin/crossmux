#pragma once

#include <Txt.h>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class TxtReaderChapterSelectionActivity final : public Activity {
  const Txt& txt;
  HalFile chapterFile;
  ButtonNavigator buttonNavigator;
  uint32_t chapterCount = 0;
  uint32_t currentOffset = 0;
  int selectorIndex = 0;

  int getPageItems() const;
  std::string chapterTitle(int index);
  void selectChapter();

 public:
  explicit TxtReaderChapterSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, const Txt& txt,
                                             uint32_t currentOffset)
      : Activity("TxtReaderChapterSelection", renderer, mappedInput), txt(txt), currentOffset(currentOffset) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
};
