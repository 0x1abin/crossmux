#include "UiContentSelection.h"

#include "MappedInputManager.h"

bool UiContentSelection::isEntryHeld(const MappedInputManager& input) {
  using Button = MappedInputManager::Button;
  return input.isPressed(Button::NavNext) || input.isPressed(Button::NavPrevious) || input.isPressed(Button::Confirm);
}

void UiContentSelection::reset(const MappedInputManager& input, const bool deferred) {
  clear();
  releasePending = deferred && isEntryHeld(input);
}

void UiContentSelection::guardHeld(const MappedInputManager& input) { releasePending = isEntryHeld(input); }

UiContentSelection::Entry UiContentSelection::consumeEntry(const MappedInputManager& input, const bool deferred,
                                                           const bool hasUsableSelection) {
  if (releasePending) {
    guardHeld(input);
    return Entry::Consumed;
  }
  if (!deferred || hasUsableSelection) return Entry::None;

  using Button = MappedInputManager::Button;
  struct Key {
    Button button;
    Entry entry;
  };
  constexpr Key keys[] = {
      {Button::NavNext, Entry::First}, {Button::NavPrevious, Entry::Last}, {Button::Confirm, Entry::Confirm}};
  for (const auto& key : keys) {
    const bool active = input.wasPressed(key.button) || input.isPressed(key.button) || input.wasReleased(key.button);
    if (active) {
      guardHeld(input);
      return key.entry;
    }
  }
  return Entry::None;
}
