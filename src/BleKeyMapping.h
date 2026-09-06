#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace bleinput {

enum class Action : uint8_t { PageForward, PageBack, Confirm, Back, Up, Down, Left, Right, Count };

struct KeyMapEntry {
  uint8_t keyKind = 0xFF;
  uint8_t keyValue = 0;
  uint8_t button = 0xFF;
};

inline constexpr size_t kKeyMapCapacity = 10;
using KeyMap = std::array<KeyMapEntry, kKeyMapCapacity>;
static_assert(sizeof(KeyMapEntry) == 3);
static_assert(sizeof(KeyMap) == 30);

constexpr bool isValidKey(const uint8_t kind, const uint8_t value) { return kind <= 1 && value != 0; }
constexpr bool isValidAction(const uint8_t action) { return action < static_cast<uint8_t>(Action::Count); }
constexpr bool isEmpty(const KeyMapEntry& entry) { return entry.keyKind == 0xFF || entry.button == 0xFF; }

constexpr const KeyMapEntry* findByAction(const KeyMap& map, const Action action) {
  for (const auto& entry : map) {
    if (!isEmpty(entry) && entry.button == static_cast<uint8_t>(action)) return &entry;
  }
  return nullptr;
}

constexpr bool lookup(const KeyMap& map, const uint8_t kind, const uint8_t value, Action& action) {
  for (const auto& entry : map) {
    if (!isEmpty(entry) && entry.keyKind == kind && entry.keyValue == value) {
      action = static_cast<Action>(entry.button);
      return true;
    }
  }
  return false;
}

// Load-time insertion: malformed and duplicate records are ignored so the
// first valid key and action in the persisted array win.
constexpr bool appendValidated(KeyMap& map, const uint8_t kind, const uint8_t value, const uint8_t action) {
  if (!isValidKey(kind, value) || !isValidAction(action)) return false;
  for (const auto& entry : map) {
    if (isEmpty(entry)) continue;
    if ((entry.keyKind == kind && entry.keyValue == value) || entry.button == action) return false;
  }
  for (auto& entry : map) {
    if (!isEmpty(entry)) continue;
    entry = {kind, value, action};
    return true;
  }
  return false;
}

// UI-time assignment: a key and an action each retain exactly one binding.
constexpr bool assign(KeyMap& map, const uint8_t kind, const uint8_t value, const Action action) {
  if (!isValidKey(kind, value)) return false;
  const uint8_t actionValue = static_cast<uint8_t>(action);
  for (auto& entry : map) {
    if (isEmpty(entry)) continue;
    if ((entry.keyKind == kind && entry.keyValue == value) || entry.button == actionValue) entry = {};
  }
  return appendValidated(map, kind, value, actionValue);
}

}  // namespace bleinput
