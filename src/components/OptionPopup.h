#pragma once
#include <I18n.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "GfxRenderer.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"

class OptionPopup {
 public:
  void show(StrId titleId, const StrId* optionIds, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    const int count = optionCount > 0 ? optionCount : 0;
    ownedStrings.resize(count);
    for (int i = 0; i < count; i++) {
      ownedStrings[i] = I18N.get(optionIds[i]);
    }
    finishShow(currentIndex, std::move(onSelect));
  }

  void show(const char* titleStr, const char* const* options, int optionCount, int currentIndex,
            std::function<void(int)> onSelect) {
    title = titleStr;
    const int count = optionCount > 0 ? optionCount : 0;
    ownedStrings.resize(count);
    for (int i = 0; i < count; i++) {
      ownedStrings[i] = options[i];
    }
    finishShow(currentIndex, std::move(onSelect));
  }

  void show(StrId titleId, const std::vector<std::string>& options, int currentIndex,
            std::function<void(int)> onSelect) {
    title = I18N.get(titleId);
    ownedStrings = options;
    finishShow(currentIndex, std::move(onSelect));
  }

  bool handleInput(MappedInputManager& input, const std::function<void()>& requestUpdate) {
    if (!active) return false;

    const int count = static_cast<int>(ownedStrings.size());
    if (count <= 0) {
      active = false;
      requestUpdate();
      return true;
    }
    if (input.wasPressed(MappedInputManager::Button::Up) || input.wasPressed(MappedInputManager::Button::Left)) {
      selectedIndex = (selectedIndex - 1 + count) % count;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Down) ||
               input.wasPressed(MappedInputManager::Button::Right)) {
      selectedIndex = (selectedIndex + 1) % count;
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Confirm)) {
      active = false;
      if (onSelectCallback) onSelectCallback(selectedIndex);
      requestUpdate();
      return true;
    } else if (input.wasPressed(MappedInputManager::Button::Back)) {
      active = false;
      requestUpdate();
      return true;
    }
    return true;
  }

  bool processRender(GfxRenderer& renderer, const MappedInputManager& input) const {
    if (!active) return false;
    const auto popupLabels = input.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, popupLabels.btn1, popupLabels.btn2, popupLabels.btn3, popupLabels.btn4);
    render(renderer);
    renderer.displayBuffer();
    return true;
  }

  void render(const GfxRenderer& renderer) const {
    if (!active) return;
    GUI.drawOptionPopup(renderer, title.c_str(), ownedStrings, selectedIndex);
  }

  bool isActive() const { return active; }

 private:
  void finishShow(int currentIndex, std::function<void(int)> onSelect) {
    const int count = static_cast<int>(ownedStrings.size());
    if (count <= 0) {
      selectedIndex = 0;
      onSelectCallback = nullptr;
      active = false;
      return;
    }
    if (currentIndex < 0) {
      selectedIndex = 0;
    } else if (currentIndex >= count) {
      selectedIndex = count - 1;
    } else {
      selectedIndex = currentIndex;
    }
    onSelectCallback = std::move(onSelect);
    active = true;
  }

  bool active = false;
  std::string title;
  std::vector<std::string> ownedStrings;
  int selectedIndex = 0;
  std::function<void(int)> onSelectCallback;
};
