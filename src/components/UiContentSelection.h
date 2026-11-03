#pragma once

#include <atomic>

class MappedInputManager;

// Page-local selection and the complete first-key gesture, shared by list and custom pages.
class UiContentSelection {
 public:
  enum class Entry { None, Consumed, Confirm, First, Last };

  void reset(const MappedInputManager& input, bool deferred);
  void clear() { established.store(false); }
  void establish() { established.store(true); }
  bool hasSelection() const { return established.load(); }
  void guardHeld(const MappedInputManager& input);
  Entry consumeEntry(const MappedInputManager& input, bool deferred, bool hasUsableSelection);

 private:
  static bool isEntryHeld(const MappedInputManager& input);
  std::atomic<bool> established{false};
  bool releasePending = false;
};
