#pragma once

// Borrowed synchronous callback: no allocation and no lifetime beyond the operation.
struct CancelCheck {
  void* context = nullptr;
  bool (*requested)(void*) = nullptr;
  bool isCancelled() const { return requested && requested(context); }
  explicit operator bool() const { return requested != nullptr; }
};
