#pragma once

#include <AvatarGen.h>
#include <AvatarGenerator.h>

#include <memory>

#include "../../Activity.h"

class UglyAvatarActivity final : public Activity {
 public:
  explicit UglyAvatarActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UglyAvatar", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
  void onExit() override;

 private:
  std::unique_ptr<avatar::AvatarGenerator> gen_;
  std::unique_ptr<avatar::AvatarData> data_;

  void regenerate();
  void onSave();
};
