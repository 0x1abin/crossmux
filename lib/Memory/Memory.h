#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#if !defined(SIMULATOR) && !defined(CROSSPOINT_EMULATED) && __has_include(<esp_heap_caps.h>)
#include <esp_heap_caps.h>
#define CROSSPOINT_MEMORY_HAS_HEAP_CAPS 1
#endif

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);
//

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

namespace memory {

// Large render buffers cannot live on the task stack and need selectable heap
// capabilities. free() is the documented owner release for heap_caps_calloc().
struct FreeDeleter final {
  void operator()(uint8_t* ptr) const noexcept { std::free(ptr); }
};

using ByteBuffer = std::unique_ptr<uint8_t, FreeDeleter>;

constexpr size_t PSRAM_FREE_RESERVE = 256 * 1024;

constexpr bool hasAllocationHeadroom(const size_t freeBytes, const size_t largestBlock, const size_t totalBytes,
                                     const size_t contiguousBytes, const size_t freeReserve,
                                     const size_t contiguousReserve) {
  return freeBytes >= freeReserve && largestBlock >= contiguousReserve && totalBytes <= freeBytes - freeReserve &&
         contiguousBytes <= largestBlock - contiguousReserve;
}

static_assert(hasAllocationHeadroom(300, 100, 40, 20, 256, 8));
static_assert(!hasAllocationHeadroom(255, 100, 0, 20, 256, 8));
static_assert(!hasAllocationHeadroom(300, 27, 40, 20, 256, 8));

inline ByteBuffer makeInternalByteBufferNoThrow(const size_t size) {
  if (size == 0) return {};
#if defined(CROSSPOINT_MEMORY_HAS_HEAP_CAPS)
  return ByteBuffer{static_cast<uint8_t*>(heap_caps_calloc(1, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT))};
#else
  return ByteBuffer{static_cast<uint8_t*>(std::calloc(1, size))};
#endif
}

inline ByteBuffer makePsramByteBufferNoThrow(const size_t size) {
#if defined(BOARD_HAS_PSRAM) && defined(CROSSPOINT_MEMORY_HAS_HEAP_CAPS)
  if (size == 0) return {};
  return ByteBuffer{static_cast<uint8_t*>(heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))};
#else
  (void)size;
  return {};
#endif
}

inline ByteBuffer makePsramByteBufferUninitializedNoThrow(const size_t size) {
#if defined(BOARD_HAS_PSRAM) && defined(CROSSPOINT_MEMORY_HAS_HEAP_CAPS)
  if (size == 0) return {};
  return ByteBuffer{static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT))};
#else
  (void)size;
  return {};
#endif
}

inline bool psramHasHeadroom(const size_t totalBytes, const size_t contiguousBytes, const size_t contiguousReserve) {
#if defined(BOARD_HAS_PSRAM) && defined(CROSSPOINT_MEMORY_HAS_HEAP_CAPS)
  return hasAllocationHeadroom(heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                               heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM), totalBytes, contiguousBytes,
                               PSRAM_FREE_RESERVE, contiguousReserve);
#else
  (void)totalBytes;
  (void)contiguousBytes;
  (void)contiguousReserve;
  return false;
#endif
}

}  // namespace memory

#undef CROSSPOINT_MEMORY_HAS_HEAP_CAPS

// Helper struct to call a cleanup function on exit from any scope.
// Use with a lambda to avoid unnecessary allocations from std::function/std::bind:
// Example:
//   auto jpeg = makeUniqueNoThrow<JPEGDEC>();
//   ScopedCleanup cleanup{[&jpeg]{ jpeg->close(); }};
//
template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ScopedCleanup(ScopedCleanup&&) = delete;
  ScopedCleanup& operator=(ScopedCleanup&&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
