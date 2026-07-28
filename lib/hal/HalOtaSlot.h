#pragma once

#include <cstddef>
#include <cstdint>

// Bounded access to the OTA application slot that is not currently running.
// This is the storage mechanism used by disposable caches; callers must not
// reach into ESP-IDF partition APIs.
class HalOtaSlot {
 public:
  enum class RunningImageState : uint8_t {
    PendingVerify,
    Confirmed,
    Untracked,
    Unsafe,
  };

  static constexpr size_t ERASE_SIZE = 4096;

  HalOtaSlot() = default;

  static HalOtaSlot inactive();
  static RunningImageState runningImageState();
  static bool confirmRunningImage();

  bool valid() const { return partition_ != nullptr; }
  size_t size() const { return size_; }
  bool safeForScratchWrite() const;
  bool read(size_t offset, void* data, size_t length) const;
  bool erase(size_t offset, size_t length) const;
  bool write(size_t offset, const void* data, size_t length) const;

 private:
  explicit HalOtaSlot(const void* partition, size_t size) : partition_(partition), size_(size) {}
  bool contains(size_t offset, size_t length) const;

  const void* partition_ = nullptr;
  size_t size_ = 0;
};
