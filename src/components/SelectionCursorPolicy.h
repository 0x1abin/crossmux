#pragma once

#include <FreeInkUICore.h>

#include "InputModality.h"

namespace SelectionCursorPolicy {

constexpr bool visible(const bool inxTheme, const bool hasTouch, const InputModality modality) {
  return !inxTheme || !hasTouch || modality == InputModality::Buttons;
}

inline void hideFreeInkListFocus(freeink::ui::ThemeTokens& tokens) {
  tokens.listRow.selected = tokens.listRow.normal;
  tokens.listRow.focused = tokens.listRow.normal;
}

}  // namespace SelectionCursorPolicy
